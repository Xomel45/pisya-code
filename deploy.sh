#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# deploy.sh — упаковка и развёртывание артефактов сборки Naleystogramm
# ══════════════════════════════════════════════════════════════════════════════
#
# Использование:
#   ./deploy.sh beta                  — сырой ELF → builds/beta/
#   ./deploy.sh release               — все платформы → builds/releases/VERSION-*/
#   ./deploy.sh release linux         — AppImage → builds/releases/VERSION-linux/
#   ./deploy.sh release win           — .exe+DLL → builds/releases/VERSION-windows/
#
# Опции:
#   --build     Пересобрать проект перед деплоем (через CMake)
#   --clean     Удалить целевую директорию перед копированием
#
# Примеры:
#   ./deploy.sh beta --build              Собрать и задеплоить beta
#   ./deploy.sh release linux --clean     AppImage поверх очищенной директории
#   ./deploy.sh release --build --clean   Полный цикл: сборка + релиз всех платформ
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Цвета и вывод ─────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

log()    { echo -e "${BLUE}[DEPLOY]${NC} $*"; }
ok()     { echo -e "${GREEN}[  OK  ]${NC} $*"; }
warn()   { echo -e "${YELLOW}[ WARN ]${NC} $*"; }
fail()   { echo -e "${RED}[ERROR ]${NC} $*" >&2; exit 1; }
header() { echo -e "\n${BOLD}${CYAN}══ $* ══${NC}"; }
rule()   { printf "${CYAN}%.0s─${NC}" {1..60}; echo; }

# ── Всегда запускаемся из корня проекта ───────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── Константы путей ───────────────────────────────────────────────────────────
APP_NAME="naleystogramm"
BUILD_LINUX="build-linux"                          # выход linux cmake
BUILD_WIN="build-win"                              # выход windows cmake (cross)
BUILDS_DIR="builds"                                # корень артефактов
WIN_MINGW_ROOT="/usr/x86_64-w64-mingw32"           # корень mingw-w64 тулчейна
WIN_QT_DLLS="${WIN_MINGW_ROOT}/bin"                # Qt6*.dll (напр. Qt6Core.dll)
WIN_QT_PLUGINS="${WIN_MINGW_ROOT}/lib/qt6/plugins" # плагины (platforms/, sqldrivers/ ...)

# ── Чтение версии из CMakeLists.txt ──────────────────────────────────────────
# Берём из строки вида: project(naleystogramm VERSION 0.3.2 LANGUAGES CXX RC)
get_version() {
    local v
    v=$(sed -n 's/^project([^ ]* VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
    echo "${v:-unknown}"
}
VERSION=$(get_version)

# ── Парсинг аргументов ────────────────────────────────────────────────────────
MODE="${1:-help}"
PLATFORM="${2:-both}"
DO_BUILD=false
DO_CLEAN=false

for arg in "$@"; do
    [[ "$arg" == "--build" ]] && DO_BUILD=true
    [[ "$arg" == "--clean" ]] && DO_CLEAN=true
done

# ── Вспомогательные функции ───────────────────────────────────────────────────

# Размер файла в человекочитаемом виде
file_size() { du -h "$1" 2>/dev/null | cut -f1 || echo "?"; }

# Получаем короткий git-хеш коммита
git_hash() { git rev-parse --short HEAD 2>/dev/null || echo "unknown"; }

# Сохраняем метаданные сборки рядом с артефактом
write_build_info() {
    local dir="$1"
    printf "version=%s\nbuilt=%s\ncommit=%s\nplatform=%s\n" \
        "$VERSION" \
        "$(date '+%Y-%m-%d %H:%M:%S')" \
        "$(git_hash)" \
        "${2:-linux}" \
        > "$dir/build-info.txt"
}

# Определяет семейство дистрибутива по /etc/os-release.
# Возвращает: pkg | deb | rpm | unknown
detect_pkg_format() {
    local id="" id_like=""
    if [[ -f /etc/os-release ]]; then
        id=$(. /etc/os-release && echo "${ID:-}")
        id_like=$(. /etc/os-release && echo "${ID_LIKE:-}")
    fi
    local combined="${id} ${id_like}"

    # ── Arch и производные ──────────────────────────────────────────────────
    local arch_re='arch|manjaro|endeavouros|garuda|artix|parabola|cachyos|blackarch'
    arch_re+='|archlabs|archcraft|arcolinux|archman|archstrike|bluestar|crystal'
    arch_re+='|ctlos|hyperbola|kaos|librewish|obarun|rebornos|anarchy|axyl|snal'
    arch_re+='|steamos'

    # ── Debian / Ubuntu и производные ───────────────────────────────────────
    local deb_re='debian|ubuntu|linuxmint|raspbian|raspios|kali|elementary|zorin'
    deb_re+='|popos|pop_os|backbox|parrot|tails|deepin|mx|antix|devuan|pureos'
    deb_re+='|sparky|lmde|bunsenlabs|crunchbang|bodhi|mobian|armbian|siduction'
    deb_re+='|solydxk|trisquel|netrunner|nitrux|regolith|q4os|peppermint|lite'
    deb_re+='|neon|kubuntu|lubuntu|xubuntu|endless|astra|knoppix|wubuntu'

    # ── RPM (Fedora / RHEL / SUSE / Mageia) и производные ──────────────────
    local rpm_re='fedora|rhel|centos|rocky|almalinux|opensuse|suse|oracle|scientific'
    rpm_re+='|springdale|eurolinux|clearos|cloudlinux|mageia|pclinuxos|openmandriva'
    rpm_re+='|rosa|nobara|ultramarine|bazzite|aurora|bluefin|coreos|qubes|asahi'
    rpm_re+='|turbolinux|vine|alt|mandriva|centos-stream|circle'

    if   echo "$combined" | grep -qiE "$arch_re"; then echo "pkg"
    elif echo "$combined" | grep -qiE "$deb_re";  then echo "deb"
    elif echo "$combined" | grep -qiE "$rpm_re";  then echo "rpm"
    else echo "unknown"
    fi
}

# Упаковываем файл или директорию в ZIP с максимальным сжатием (-9).
# make_zip <src> <out.zip>
# Если src — директория, упаковывает её содержимое (пути относительны родителю).
# Если src — файл, упаковывает сам файл.
make_zip() {
    local src="$1" zip_out="$2"
    local abs_zip
    abs_zip="$(realpath -m "$zip_out")"
    log "ZIP: $(basename "$abs_zip")..."
    rm -f "$abs_zip"
    if [[ -d "$src" ]]; then
        (cd "$(dirname "$src")" && zip -9 -r "$abs_zip" "$(basename "$src")")
    else
        (cd "$(dirname "$src")" && zip -9 "$abs_zip" "$(basename "$src")")
    fi
    ok "  + $(basename "$abs_zip")  ($(file_size "$abs_zip"))"
}

# Упаковываем СОДЕРЖИМОЕ директории в ZIP без обёрточной папки (-9).
# make_zip_flat <src_dir> <out.zip>
# Нужно для payload.zip инсталлера: extract_zip_from_memory распаковывает
# пути из архива как есть в install_path, а ярлыки/firewall/registry/autostart
# в install.c ожидают naleystogramm.exe сразу в install_path, без вложенной
# папки вида "0.8.2-windows/".
make_zip_flat() {
    local src_dir="$1" zip_out="$2"
    local abs_zip
    abs_zip="$(realpath -m "$zip_out")"
    log "ZIP (flat): $(basename "$abs_zip")..."
    rm -f "$abs_zip"
    (cd "$src_dir" && zip -9 -r "$abs_zip" .)
    ok "  + $(basename "$abs_zip")  ($(file_size "$abs_zip"))"
}

# Копируем файл с логированием; при отсутствии — warn, не fail
copy_file() {
    local src="$1" dst="$2" label="$3"
    if [[ -f "$src" ]]; then
        cp "$src" "$dst"
        ok "  + ${label:-$(basename "$src")}"
        return 0
    else
        warn "  ? Не найден: ${label:-$(basename "$src")}"
        return 1
    fi
}

# Создаём структуру builds/ если её нет (не добавлять в git!)
ensure_builds_tree() {
    mkdir -p "$BUILDS_DIR/beta"
    mkdir -p "$BUILDS_DIR/releases"
}

# L-3: безопасное удаление — только внутри BUILDS_DIR.
# Защищает от path-traversal через VERSION (например, "../../../etc").
# Использует realpath -m (без требования существования пути).
safe_clean() {
    local target="$1"
    local abs_builds abs_target
    abs_builds="$(realpath -m "$BUILDS_DIR")"
    abs_target="$(realpath -m "$target")"

    if [[ "$abs_target" != "$abs_builds/"* && "$abs_target" != "$abs_builds" ]]; then
        fail "safe_clean: цель '${target}' (→ ${abs_target}) находится вне ${abs_builds} — отказ в удалении!"
    fi

    log "Очистка $target ..."
    rm -rf "${target:?}"
}

# ── Сборка (опциональная) ─────────────────────────────────────────────────────

build_linux() {
    header "Сборка Linux (Release)"
    cmake -B "$BUILD_LINUX" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/usr/lib/qt6
    cmake --build "$BUILD_LINUX" --parallel "$(( $(nproc) - 2 ))"
    ok "Linux бинарник собран: $BUILD_LINUX/$APP_NAME"
}

build_windows() {
    header "Сборка Windows (cross-compile MinGW)"
    cmake -B "$BUILD_WIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake
    cmake --build "$BUILD_WIN" --parallel "$(( $(nproc) - 2 ))"
    ok "Windows .exe собран: $BUILD_WIN/$APP_NAME.exe"
}

# ── РЕЖИМ: Beta ───────────────────────────────────────────────────────────────
# Назначение: быстрая проверка, итерации.
# Артефакт:   сырой ELF без AppImage-упаковки (нет зависимостей Qt рядом!).
# Путь:       builds/beta/naleystogramm
deploy_beta() {
    header "Beta Deploy → ${BUILDS_DIR}/beta/"
    ensure_builds_tree

    # Сборка если запрошена или бинарника нет
    if $DO_BUILD || [[ ! -f "$BUILD_LINUX/$APP_NAME" ]]; then
        build_linux
    fi

    local dest="${BUILDS_DIR}/beta"

    # Опциональная очистка
    $DO_CLEAN && { safe_clean "$dest"; }

    # Копирование ELF
    cp "$BUILD_LINUX/$APP_NAME" "$dest/$APP_NAME"
    chmod +x "$dest/$APP_NAME"

    # Копируем переводы (нужны при запуске из этой же директории)
    if [[ -d "$BUILD_LINUX/translations" ]]; then
        mkdir -p "$dest/translations"
        cp "$BUILD_LINUX/translations/"*.qm "$dest/translations/" 2>/dev/null || true
        ok "  + translations/"
    fi

    write_build_info "$dest" "linux-beta"

    rule
    ok "Beta готов!"
    echo "  Путь:    ${BOLD}$dest/$APP_NAME${NC}"
    echo "  Размер:  $(file_size "$dest/$APP_NAME")"
    echo "  Версия:  $VERSION  |  commit: $(git_hash)"
    echo ""
    echo -e "  ${YELLOW}Запуск (требует Qt6 в системе):${NC}"
    echo "    ./$dest/$APP_NAME"
    echo ""
}

# ── РЕЖИМ: Release Linux (AppImage) ──────────────────────────────────────────
# Назначение: финальный дистрибутив для Linux.
# Артефакт:   самодостаточный AppImage (Qt + плагины + переводы внутри).
# Путь:       builds/releases/VERSION-linux/Naleystogramm-x86_64.AppImage
deploy_release_linux() {
    header "Release Linux → ${BUILDS_DIR}/releases/${VERSION}-linux/"
    ensure_builds_tree

    # Сборка если запрошена или бинарника нет
    if $DO_BUILD || [[ ! -f "$BUILD_LINUX/$APP_NAME" ]]; then
        build_linux
    fi

    local dest="${BUILDS_DIR}/releases/${VERSION}-linux"
    local appimage_name="Naleystogramm-x86_64.AppImage"

    # Опциональная очистка
    $DO_CLEAN && { safe_clean "$dest"; }
    mkdir -p "$dest"

    # Запускаем make_appimage.sh из BUILD_LINUX чтобы linuxdeploy создал
    # AppImage именно там (он помещает .AppImage в рабочую директорию)
    log "Запуск make_appimage.sh..."
    (
        cd "$BUILD_LINUX"
        bash "$SCRIPT_DIR/scripts/make_appimage.sh" "$SCRIPT_DIR/$BUILD_LINUX"
    )

    # Находим созданный AppImage (make_appimage.sh кладёт его в BUILD_LINUX/)
    local created_appimage
    created_appimage=$(ls "$BUILD_LINUX"/Naleystogramm-*.AppImage 2>/dev/null | sort -V | tail -1 || true)

    if [[ -z "$created_appimage" || ! -f "$created_appimage" ]]; then
        fail "AppImage не найден в $BUILD_LINUX/ после сборки! Проверь вывод make_appimage.sh."
    fi

    # Переносим в релизную директорию с правильным именем (версия из CMakeLists.txt)
    cp "$created_appimage" "$dest/$appimage_name"
    chmod +x "$dest/$appimage_name"

    write_build_info "$dest" "linux"

    rule
    ok "Release Linux готов!"
    echo "  Директория:  ${BOLD}$dest/${NC}"
    echo "  AppImage:    $appimage_name"
    echo "  Размер:      $(file_size "$dest/$appimage_name")"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Запуск (переносимый, Qt не нужен в системе):"
    echo "    chmod +x $dest/$appimage_name"
    echo "    ./$dest/$appimage_name"
    echo ""
}

# ── РЕЖИМ: Release Windows (.exe + DLL) ──────────────────────────────────────
# Назначение: финальный дистрибутив для Windows.
# Артефакт:   папка ready-to-run: .exe + Qt DLL + плагины + переводы.
# Путь:       builds/releases/VERSION-windows/
#
# Статически встроены в .exe (DLL НЕ нужны):
#   libssl.a / libcrypto.a  — OPENSSL_USE_STATIC_LIBS=TRUE
#   libgcc, libstdc++       — -static-libgcc / -static-libstdc++
# Копируется в пакет (нужна Qt6Core.dll):
#   libwinpthread-1.dll     — Qt6Core.dll динамически импортирует её
deploy_release_windows() {
    header "Release Windows → ${BUILDS_DIR}/releases/${VERSION}-windows/"
    ensure_builds_tree

    build_windows

    local dest="${BUILDS_DIR}/releases/${VERSION}-windows"

    # Опциональная очистка
    $DO_CLEAN && { safe_clean "$dest"; }

    # Создаём структуру директорий пакета
    mkdir -p "$dest"/{platforms,sqldrivers,styles,tls,networkinformation,translations}

    local ok_count=0
    local warn_count=0

    # Обёртка copy_file с подсчётом
    copy_tracked() {
        if copy_file "$@"; then
            (( ok_count++ )) || true
        else
            (( warn_count++ )) || true
        fi
    }

    log "Копирование .exe..."
    copy_tracked "$BUILD_WIN/$APP_NAME.exe" "$dest/$APP_NAME.exe" "$APP_NAME.exe"

    log "Копирование Qt6 основных DLL..."
    for dll in Qt6Core Qt6Widgets Qt6Network Qt6Gui Qt6Multimedia; do
        copy_tracked "${WIN_QT_DLLS}/${dll}.dll" "$dest/${dll}.dll" "${dll}.dll"
    done

    # Мультимедиа бэкенды (опционально: нужны для воспроизведения голосовых)
    mkdir -p "$dest/multimedia"
    for mm_dll in "${WIN_QT_PLUGINS}/multimedia/"*.dll; do
        [[ -f "$mm_dll" ]] && copy_tracked "$mm_dll" \
            "$dest/multimedia/$(basename "$mm_dll")" \
            "multimedia/$(basename "$mm_dll")"
    done

    # Платформенный плагин — КРИТИЧНО: QApplication упадёт без него!
    log "Копирование Qt6 платформенного плагина..."
    copy_tracked \
        "${WIN_QT_PLUGINS}/platforms/qwindows.dll" \
        "$dest/platforms/qwindows.dll" \
        "platforms/qwindows.dll  ← без него крэш!"

    log "Копирование Qt6 плагинов..."
    # Qt6.8+: qmodernwindowsstyle.dll; старые версии: qwindowsvistastyle.dll
    for style_dll in qmodernwindowsstyle qwindowsvistastyle; do
        if [[ -f "${WIN_QT_PLUGINS}/styles/${style_dll}.dll" ]]; then
            copy_tracked \
                "${WIN_QT_PLUGINS}/styles/${style_dll}.dll" \
                "$dest/styles/${style_dll}.dll" \
                "styles/${style_dll}.dll"
            break
        fi
    done

    # TLS плагин (для Qt Network / TLS соединений)
    for tls_dll in "${WIN_QT_PLUGINS}/tls/"*.dll; do
        [[ -f "$tls_dll" ]] && copy_tracked "$tls_dll" "$dest/tls/$(basename "$tls_dll")" \
            "tls/$(basename "$tls_dll")"
    done

    # Network information плагин
    for ni_dll in "${WIN_QT_PLUGINS}/networkinformation/"*.dll; do
        [[ -f "$ni_dll" ]] && copy_tracked "$ni_dll" \
            "$dest/networkinformation/$(basename "$ni_dll")" \
            "networkinformation/$(basename "$ni_dll")"
    done

    # Переводы приложения
    log "Копирование переводов..."
    if [[ -d "$BUILD_WIN/translations" ]]; then
        cp "$BUILD_WIN/translations/"*.qm "$dest/translations/" 2>/dev/null || true
        ok "  + translations/*.qm"
    fi

    # MinGW runtime + транзитивные зависимости Qt — полный список, выявлен
    # рекурсивным анализом objdump по всем DLL в пакете.
    # Системные DLL (kernel32, user32, api-ms-win-*, d3d*, dwrite и т.д.)
    # не включаются — они присутствуют на любой Windows 10+.
    log "Копирование MinGW runtime и зависимостей Qt..."
    for rt_dll in \
        libgcc_s_seh-1.dll \
        libstdc++-6.dll \
        libssp-0.dll \
        libwinpthread-1.dll \
        zlib1.dll \
        libpng16-16.dll \
        libfreetype-6.dll \
        libbrotlidec.dll \
        libbrotlicommon.dll \
        libbz2-1.dll \
        libharfbuzz-0.dll \
        libglib-2.0-0.dll \
        libgraphite2.dll \
        libintl-8.dll \
        libiconv-2.dll \
        libpcre2-8-0.dll \
        libpcre2-16-0.dll \
        libsqlite3-0.dll \
        libzstd.dll; do
        copy_tracked \
            "${WIN_MINGW_ROOT}/bin/${rt_dll}" \
            "$dest/${rt_dll}" \
            "${rt_dll}"
    done

    # Статусная информация
    log "  OpenSSL:  статически встроен в .exe (DLL не нужны)"
    log "  MinGW runtime: libgcc/libstdc++/libssp/libwinpthread — скопированы (требуют Qt DLL)"

    # Метаданные сборки
    write_build_info "$dest" "windows"

    # README для конечного пользователя (на русском и английском)
    cat > "$dest/README.txt" << EOF
Naleystogramm v${VERSION} — Windows Release
============================================

Требования / Requirements:
  - Windows 10 / 11 (x86_64)
  - Права Администратора (UAC встроен в .exe / Admin rights built-in)

Запуск / Launch:
  Двойной клик → Windows запросит права Администратора → разрешить.
  Double-click → Windows will ask for Admin rights → allow.

Структура папки:
  naleystogramm.exe         — основной исполняемый файл
  platforms/qwindows.dll    — Qt платформа Windows (обязательна!)
  styles/                   — Qt стили Windows 10/11
  tls/                      — Qt TLS плагины (зашифрованные соединения)
  networkinformation/        — Qt сетевая информация
  translations/             — переводы интерфейса

Встроено статически (отдельные DLL не нужны):
  OpenSSL ${VERSION} — шифрование (libssl.a / libcrypto.a)
  MinGW runtime    — libgcc, libstdc++, libpthread

Версия: ${VERSION}
Сборка: $(date '+%Y-%m-%d')
Коммит: $(git_hash)
EOF
    ok "  + README.txt"

    make_zip "$dest" "${BUILDS_DIR}/releases/naleystogramm-windows.zip"

    rule
    ok "Release Windows готов!"
    echo "  Директория:  ${BOLD}$dest/${NC}"
    echo "  ZIP:         naleystogramm-windows.zip  ($(file_size "${BUILDS_DIR}/releases/naleystogramm-windows.zip"))"
    echo "  .exe:        $APP_NAME.exe  ($(file_size "$dest/$APP_NAME.exe"))"
    echo "  Скопировано: $ok_count  |  Пропущено: $warn_count"
    echo ""
    echo "  Содержимое пакета:"
    find "$dest" -type f | sort | while IFS= read -r f; do
        printf "    %-52s %s\n" "${f#"$dest"/}" "$(file_size "$f")"
    done
    echo ""
    echo -e "  ${YELLOW}При двойном клике на .exe — Windows запросит права Администратора${NC}"
    echo "  (UAC requireAdministrator встроен через windres в PE-ресурс)"
    echo ""
}

# ── РЕЖИМ: Release Linux (.pkg.tar.zst — Arch Linux) ─────────────────────────
# Назначение: нативный пакет для Arch Linux / pacman.
# Артефакт:   naleystogramm-x86_64.pkg.tar.zst
# Путь:       builds/releases/VERSION-linux/
# Установка:  sudo pacman -U naleystogramm-x86_64.pkg.tar.zst
deploy_release_pkg() {
    header "Release .pkg.tar.zst → ${BUILDS_DIR}/releases/${VERSION}-linux/"
    ensure_builds_tree

    if ! command -v zstd &>/dev/null; then
        fail "zstd не найден. Установите: sudo pacman -S zstd"
    fi

    if $DO_BUILD || [[ ! -f "$BUILD_LINUX/$APP_NAME" ]]; then
        build_linux
    fi

    local dest="${BUILDS_DIR}/releases/${VERSION}-linux"
    local pkgver="${VERSION}-1"
    local pkg_name="naleystogramm-x86_64.pkg.tar.zst"
    local staging="${BUILD_LINUX}/pkg-staging"

    $DO_CLEAN && { safe_clean "$dest"; }
    mkdir -p "$dest"

    rm -rf "$staging"
    mkdir -p "$staging/usr/bin"
    mkdir -p "$staging/usr/share/applications"
    mkdir -p "$staging/usr/share/icons/hicolor/256x256/apps"
    mkdir -p "$staging/usr/share/translations/naleystogramm"

    cp "$BUILD_LINUX/$APP_NAME" "$staging/usr/bin/"
    chmod 755 "$staging/usr/bin/$APP_NAME"
    ok "  + usr/bin/$APP_NAME"

    if [[ -d "$BUILD_LINUX/translations" ]]; then
        cp "$BUILD_LINUX/translations/"*.qm "$staging/usr/share/translations/naleystogramm/" 2>/dev/null || true
        ok "  + usr/share/translations/naleystogramm/"
    fi

    if [[ -f "naleystogramm.desktop" ]]; then
        cp "naleystogramm.desktop" "$staging/usr/share/applications/"
        ok "  + usr/share/applications/naleystogramm.desktop"
    fi

    if [[ -f "resources/icons/app_icon.png" ]]; then
        cp "resources/icons/app_icon.png" "$staging/usr/share/icons/hicolor/256x256/apps/naleystogramm.png"
        ok "  + usr/share/icons/hicolor/256x256/apps/naleystogramm.png"
    fi

    local installed_size
    installed_size=$(du -sb "$staging" | cut -f1)

    cat > "$staging/.PKGINFO" << PKGINFO_EOF
pkgname = naleystogramm
pkgver = ${pkgver}
pkgdesc = P2P мессенджер с E2E-шифрованием без центрального сервера
url = https://github.com/xomel45/naleystogramm
builddate = $(date +%s)
packager = xomel45 <xom.xom.zip@gmail.com>
size = ${installed_size}
arch = x86_64
depend = qt6-base
depend = openssl
optdepend = qt6-multimedia: голосовые сообщения
optdepend = opus: голосовые звонки
optdepend = qrencode: QR-код для привязки устройств
PKGINFO_EOF
    ok "  + .PKGINFO  (depends: qt6-base, openssl)"

    tar --zstd -cf "$dest/$pkg_name" -C "$staging" .PKGINFO usr
    rm -rf "$staging"

    rule
    ok "Release .pkg.tar.zst готов!"
    echo "  Директория:  ${BOLD}$dest/${NC}"
    echo "  Пакет:       $pkg_name"
    echo "  Размер:      $(file_size "$dest/$pkg_name")"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Установка:"
    echo "    sudo pacman -U $dest/$pkg_name"
    echo ""
}

# ── РЕЖИМ: Release Linux (.deb) ───────────────────────────────────────────────
# Назначение: пакет для Debian / Ubuntu / Mint (системная установка).
# Артефакт:   naleystogramm_amd64.deb
# Путь:       builds/releases/VERSION-linux/
# Требует:    dpkg-deb  (пакет dpkg-dev)
deploy_release_deb() {
    header "Release .deb → ${BUILDS_DIR}/releases/${VERSION}-linux/"
    ensure_builds_tree

    if ! command -v dpkg-deb &>/dev/null; then
        fail "dpkg-deb не найден. Установите: sudo pacman -S dpkg  /  sudo apt install dpkg-dev"
    fi

    if $DO_BUILD || [[ ! -f "$BUILD_LINUX/$APP_NAME" ]]; then
        build_linux
    fi

    local dest="${BUILDS_DIR}/releases/${VERSION}-linux"
    local deb_name="naleystogramm_amd64.deb"
    local pkg_dir="${BUILD_LINUX}/deb-staging"

    $DO_CLEAN && { safe_clean "$dest"; }
    mkdir -p "$dest"

    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir/DEBIAN"
    mkdir -p "$pkg_dir/usr/bin"
    mkdir -p "$pkg_dir/usr/share/applications"
    mkdir -p "$pkg_dir/usr/share/icons/hicolor/256x256/apps"
    mkdir -p "$pkg_dir/usr/share/translations/naleystogramm"

    local installed_kb
    installed_kb=$(du -sk "$BUILD_LINUX/$APP_NAME" | cut -f1)

    cat > "$pkg_dir/DEBIAN/control" << CTRL_EOF
Package: naleystogramm
Version: ${VERSION}
Section: net
Priority: optional
Architecture: amd64
Installed-Size: ${installed_kb}
Depends: libqt6core6 | libqt6core6t64, libqt6gui6 | libqt6gui6t64, libqt6widgets6 | libqt6widgets6t64, libqt6network6 | libqt6network6t64, libssl3 | libssl1.1
Recommends: libqt6multimedia6 | libqt6multimedia6t64, libopus0, libqrencode4
Maintainer: xomel45 <xom.xom.zip@gmail.com>
Homepage: https://github.com/xomel45/naleystogramm
Description: P2P мессенджер с E2E-шифрованием без центрального сервера
 Naleystogramm — децентрализованный мессенджер на основе P2P TCP.
 Использует X3DH и Double Ratchet для end-to-end шифрования каждого сообщения.
 Работает без центрального сервера, поддерживает UPnP и relay-режим для NAT.
CTRL_EOF

    cp "$BUILD_LINUX/$APP_NAME" "$pkg_dir/usr/bin/"
    chmod 755 "$pkg_dir/usr/bin/$APP_NAME"
    ok "  + usr/bin/$APP_NAME"

    if [[ -d "$BUILD_LINUX/translations" ]]; then
        cp "$BUILD_LINUX/translations/"*.qm "$pkg_dir/usr/share/translations/naleystogramm/" 2>/dev/null || true
        ok "  + usr/share/translations/naleystogramm/"
    fi

    if [[ -f "naleystogramm.desktop" ]]; then
        cp "naleystogramm.desktop" "$pkg_dir/usr/share/applications/"
        ok "  + usr/share/applications/naleystogramm.desktop"
    fi

    if [[ -f "resources/icons/app_icon.png" ]]; then
        cp "resources/icons/app_icon.png" "$pkg_dir/usr/share/icons/hicolor/256x256/apps/naleystogramm.png"
        ok "  + usr/share/icons/hicolor/256x256/apps/naleystogramm.png"
    fi

    find "$pkg_dir/usr/share" -type f -exec chmod 644 {} \;
    find "$pkg_dir" -type d -exec chmod 755 {} \;

    dpkg-deb --build --root-owner-group "$pkg_dir" "$dest/$deb_name"
    rm -rf "$pkg_dir"

    rule
    ok "Release .deb готов!"
    echo "  Директория:  ${BOLD}$dest/${NC}"
    echo "  Пакет:       $deb_name"
    echo "  Размер:      $(file_size "$dest/$deb_name")"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Установка:"
    echo "    sudo dpkg -i $deb_name"
    echo "    sudo apt-get install -f   # если нужно подтянуть зависимости"
    echo ""
}

# ── РЕЖИМ: Release Linux (.rpm) ───────────────────────────────────────────────
# Назначение: пакет для Fedora / RHEL / openSUSE / Arch (системная установка).
# Артефакт:   naleystogramm-x86_64.rpm
# Путь:       builds/releases/VERSION-linux/
# Требует:    rpmbuild  (пакет rpm-build / rpm-tools)
deploy_release_rpm() {
    header "Release .rpm → ${BUILDS_DIR}/releases/${VERSION}-linux/"
    ensure_builds_tree

    if ! command -v rpmbuild &>/dev/null; then
        fail "rpmbuild не найден. Установите: sudo pacman -S rpm-tools  /  sudo dnf install rpm-build  /  sudo apt install rpm"
    fi

    if $DO_BUILD || [[ ! -f "$BUILD_LINUX/$APP_NAME" ]]; then
        build_linux
    fi

    local dest="${BUILDS_DIR}/releases/${VERSION}-linux"
    # RPM не допускает дефисы в Version — заменяем на подчёркивания
    local rpm_version="${VERSION//-/_}"
    local rpm_name="naleystogramm-x86_64.rpm"
    local rpm_topdir="${BUILD_LINUX}/rpm-staging"
    local buildroot="${rpm_topdir}/BUILDROOT/naleystogramm-${rpm_version}-1.x86_64"

    $DO_CLEAN && { safe_clean "$dest"; }
    mkdir -p "$dest"

    rm -rf "$rpm_topdir"
    mkdir -p "${rpm_topdir}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    mkdir -p "$buildroot/usr/bin"
    mkdir -p "$buildroot/usr/share/applications"
    mkdir -p "$buildroot/usr/share/icons/hicolor/256x256/apps"
    mkdir -p "$buildroot/usr/share/translations/naleystogramm"

    cp "$BUILD_LINUX/$APP_NAME" "$buildroot/usr/bin/"
    chmod 755 "$buildroot/usr/bin/$APP_NAME"
    ok "  + usr/bin/$APP_NAME"

    if [[ -d "$BUILD_LINUX/translations" ]]; then
        cp "$BUILD_LINUX/translations/"*.qm "$buildroot/usr/share/translations/naleystogramm/" 2>/dev/null || true
        ok "  + usr/share/translations/naleystogramm/"
    fi

    if [[ -f "naleystogramm.desktop" ]]; then
        cp "naleystogramm.desktop" "$buildroot/usr/share/applications/"
        ok "  + usr/share/applications/naleystogramm.desktop"
    fi

    if [[ -f "resources/icons/app_icon.png" ]]; then
        cp "resources/icons/app_icon.png" "$buildroot/usr/share/icons/hicolor/256x256/apps/naleystogramm.png"
        ok "  + usr/share/icons/hicolor/256x256/apps/naleystogramm.png"
    fi

    # Список файлов для %files — сканируем buildroot после заполнения
    local files_list
    files_list=$(find "$buildroot" \( -type f -o -type l \) \
        | sed "s|${buildroot}||" | sort)

    local changelog_date
    changelog_date=$(LC_ALL=C date '+%a %b %d %Y')

    cat > "${rpm_topdir}/SPECS/naleystogramm.spec" << SPEC_EOF
Name:           naleystogramm
Version:        ${rpm_version}
Release:        1
Summary:        P2P мессенджер с E2E-шифрованием без центрального сервера
License:        Proprietary
URL:            https://github.com/xomel45/naleystogramm
BuildArch:      x86_64
Requires:       qt6-qtbase >= 6.0, openssl-libs
Suggests:       qt6-qtmultimedia, opus-libs, qrencode-libs
%define __spec_install_pre %{nil}
%define _unpackaged_files_terminate_build 0

%description
Naleystogramm — децентрализованный мессенджер на основе P2P TCP.
Использует X3DH и Double Ratchet для end-to-end шифрования каждого сообщения.
Работает без центрального сервера, поддерживает UPnP и relay-режим для NAT.

%build
# pre-built binary

%install
# buildroot already populated externally

%files
$(echo "$files_list")

%changelog
* ${changelog_date} xomel45 <xom.xom.zip@gmail.com> - ${rpm_version}-1
- Release ${VERSION}
SPEC_EOF

    rpmbuild -bb \
        --nodeps \
        --define "_topdir $(realpath "$rpm_topdir")" \
        --buildroot "$(realpath "$buildroot")" \
        "${rpm_topdir}/SPECS/naleystogramm.spec"

    local created_rpm
    created_rpm=$(find "${rpm_topdir}/RPMS" -name "*.rpm" | head -1 || true)

    if [[ -z "$created_rpm" || ! -f "$created_rpm" ]]; then
        fail ".rpm не создан! Проверь вывод rpmbuild выше."
    fi

    cp "$created_rpm" "$dest/$rpm_name"
    rm -rf "$rpm_topdir"

    rule
    ok "Release .rpm готов!"
    echo "  Директория:  ${BOLD}$dest/${NC}"
    echo "  Пакет:       $rpm_name"
    echo "  Размер:      $(file_size "$dest/$rpm_name")"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Установка:"
    echo "    sudo rpm -i $rpm_name                 # RPM-based"
    echo "    sudo dnf install ./$rpm_name          # Fedora / RHEL"
    echo ""
}

# ── РЕЖИМ: Release Windows Installer (setup.exe) ──────────────────────────────
# Назначение: собственный GUI-установщик (чистый C + Win32 API).
# Пайплайн:
#   1. Собрать основной .exe + DLL (deploy_release_windows — если нет)
#   2. Упаковать папку релиза в payload.zip
#   3. Скопировать payload.zip в installer/
#   4. Собрать installer через CMake (MinGW cross)
#   5. Скопировать setup.exe в builds/releases/VERSION-windows-installer/
deploy_release_windows_installer() {
    header "Release Windows Installer → ${BUILDS_DIR}/releases/${VERSION}-windows-installer/"
    ensure_builds_tree

    local win_dir="${BUILDS_DIR}/releases/${VERSION}-windows"
    local inst_dir="${BUILDS_DIR}/releases/${VERSION}-windows-installer"
    local payload_zip="installer/payload.zip"

    # ── Шаг 1: убедиться что Windows-релиз собран ─────────────────────────────
    if [[ ! -d "$win_dir" ]] || [[ ! -f "$win_dir/$APP_NAME.exe" ]]; then
        log "Windows-релиз не найден, собираем..."
        deploy_release_windows
    else
        ok "Windows-релиз уже есть: $win_dir/"
    fi

    # ── Шаг 2: создать payload.zip из папки релиза (без обёрточной папки) ─────
    log "Упаковка payload.zip..."
    make_zip_flat "$win_dir" "$payload_zip"
    ok "payload.zip → $(file_size "$payload_zip")"

    # ── Шаг 3: собрать инсталлер (MinGW cross) ────────────────────────────────
    log "Сборка инсталлера (C + Win32 API)..."
    cmake -B "$BUILD_WIN" \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_WIN" --target naleystogramm-setup --parallel "$(( $(nproc) - 2 ))"

    local setup_exe="${BUILD_WIN}/naleystogramm-setup.exe"
    if [[ ! -f "$setup_exe" ]]; then
        fail "naleystogramm-setup.exe не найден после сборки!"
    fi

    # ── Шаг 4: копируем в releases/ ───────────────────────────────────────────
    $DO_CLEAN && { safe_clean "$inst_dir"; }
    mkdir -p "$inst_dir"
    cp "$setup_exe" "$inst_dir/naleystogramm-setup.exe"
    cp "installer/install.ps1" "$inst_dir/install.ps1"

    write_build_info "$inst_dir" "windows-installer"

    rule
    ok "Windows Installer готов!"
    echo "  Директория:  ${BOLD}$inst_dir/${NC}"
    echo "  Файлы:       naleystogramm-setup.exe  ($(file_size "$inst_dir/naleystogramm-setup.exe"))"
    echo "               install.ps1  (PowerShell-альтернатива)"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo -e "  ${YELLOW}Запустить на Windows:${NC} naleystogramm-setup.exe"
    echo "  (потребует права Администратора — UAC встроен)"
    echo ""
}

# ── Вывод помощи ─────────────────────────────────────────────────────────────
show_help() {
    echo ""
    echo -e "${BOLD}${CYAN}deploy.sh${NC} — упаковщик артефактов Naleystogramm v${BOLD}${VERSION}${NC}"
    rule
    echo ""
    echo -e "  ${BOLD}Использование:${NC}"
    echo "    ./deploy.sh beta                  Сырой ELF → builds/beta/"
    echo "    ./deploy.sh release               AppImage + Windows → builds/releases/VERSION-*/"
    echo "    ./deploy.sh release linux         AppImage → builds/releases/${VERSION}-linux/"
    echo "    ./deploy.sh release pkg/arch      .pkg.tar.zst (Arch/pacman) → builds/releases/${VERSION}-linux/"
    echo "    ./deploy.sh release deb/debian    .deb → builds/releases/${VERSION}-linux/"
    echo "    ./deploy.sh release rpm/rh        .rpm → builds/releases/${VERSION}-linux/"
    echo "    ./deploy.sh release my            Авто: собирает пакет для текущего дистрибутива"
    echo "    ./deploy.sh release linux-all     Все Linux форматы → builds/releases/${VERSION}-linux/"
    echo "    ./deploy.sh release win           Собрать + .exe+DLL+zip → builds/releases/${VERSION}-windows/"
    echo "    ./deploy.sh release win-installer Собрать GUI setup.exe → builds/releases/${VERSION}-windows-installer/"
    echo "    ./deploy.sh release all           Всё: AppImage+pkg+deb+rpm + Windows+zip"
    echo ""
    echo -e "  ${BOLD}Опции:${NC}"
    echo "    --clean     Удалить целевые директории перед сборкой"
    echo ""
    echo -e "  ${BOLD}Примеры:${NC}"
    echo "    ./deploy.sh beta --build                   # собрать + beta"
    echo "    ./deploy.sh release linux --clean          # AppImage поверх чистой папки"
    echo "    ./deploy.sh release linux-all --build      # все Linux пакеты"
    echo "    ./deploy.sh release all --build --clean    # полный цикл всех форматов"
    echo ""
    echo -e "  ${BOLD}Требования для Linux-пакетов:${NC}"
    echo "    AppImage        — linuxdeploy (скачивается автоматически)"
    echo "    .pkg.tar.zst   — zstd  (sudo pacman -S zstd)"
    echo "    .deb            — dpkg-deb  (sudo pacman -S dpkg  /  sudo apt install dpkg-dev)"
    echo "    .rpm       — rpmbuild  (sudo pacman -S rpm-tools  /  sudo dnf install rpm-build)"
    echo ""
    echo -e "  ${BOLD}Структура вывода:${NC}"
    echo "    builds/"
    echo "    ├── beta/"
    echo "    │   ├── naleystogramm          (Linux ELF, требует Qt6 в системе)"
    echo "    │   ├── translations/"
    echo "    │   └── build-info.txt"
    echo "    └── releases/"
    echo "        ├── ${VERSION}-linux/"
    echo "        │   ├── Naleystogramm-x86_64.AppImage"
    echo "        │   ├── naleystogramm-x86_64.pkg.tar.zst"
    echo "        │   ├── naleystogramm_amd64.deb"
    echo "        │   ├── naleystogramm-x86_64.rpm"
    echo "        │   └── build-info.txt"
    echo "        └── ${VERSION}-windows/"
    echo "            ├── naleystogramm.exe"
    echo "            ├── platforms/qwindows.dll"
    echo "            ├── styles/..."
    echo "            ├── translations/"
    echo "            ├── README.txt"
    echo "            └── build-info.txt"
    echo ""
    echo -e "  ${CYAN}CMake targets (альтернатива):${NC}"
    echo "    cmake --build build-linux --target beta"
    echo "    cmake --build build-linux --target release-linux"
    echo "    cmake --build build-win   --target release-windows"
    echo ""
}

# ── Точка входа ───────────────────────────────────────────────────────────────
case "$MODE" in
    beta)
        deploy_beta
        ;;
    release)
        case "$PLATFORM" in
            linux)
                deploy_release_linux
                ;;
            win|windows)
                deploy_release_windows
                ;;
            win-installer|windows-installer|installer)
                deploy_release_windows_installer
                ;;
            pkg|arch|Arch)
                deploy_release_pkg
                ;;
            deb|debian|Debian)
                deploy_release_deb
                ;;
            rpm|rh|RH|red-hat|Red-Hat|red_hat|Red_Hat)
                deploy_release_rpm
                ;;
            my)
                local fmt
                fmt=$(detect_pkg_format)
                case "$fmt" in
                    pkg) log "Определён дистрибутив: Arch-семейство → .pkg.tar.zst"; deploy_release_pkg ;;
                    deb) log "Определён дистрибутив: Debian-семейство → .deb";        deploy_release_deb ;;
                    rpm) log "Определён дистрибутив: RPM-семейство → .rpm";           deploy_release_rpm ;;
                    *)   fail "Не удалось определить семейство дистрибутива. Укажи формат вручную: pkg / deb / rpm" ;;
                esac
                ;;
            linux-all)
                # Чистим общую linux-папку один раз, чтобы каждый формат
                # не стирал артефакты предыдущего.
                if $DO_CLEAN; then
                    ensure_builds_tree
                    safe_clean "${BUILDS_DIR}/releases/${VERSION}-linux"
                    DO_CLEAN=false
                fi
                deploy_release_linux
                deploy_release_pkg
                deploy_release_deb
                deploy_release_rpm
                ;;
            all)
                if $DO_CLEAN; then
                    ensure_builds_tree
                    safe_clean "${BUILDS_DIR}/releases/${VERSION}-linux"
                    safe_clean "${BUILDS_DIR}/releases/${VERSION}-windows"
                    safe_clean "${BUILDS_DIR}/releases/${VERSION}-windows-installer"
                    rm -f "${BUILDS_DIR}/releases/naleystogramm-windows.zip"
                    DO_CLEAN=false
                fi
                deploy_release_linux
                deploy_release_pkg
                deploy_release_deb
                deploy_release_rpm
                deploy_release_windows
                deploy_release_windows_installer
                ;;
            both|--build|--clean|*)
                # Если второй аргумент — опция, значит AppImage + Windows
                deploy_release_linux
                deploy_release_windows
                ;;
        esac
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        show_help
        fail "Неизвестная команда: '$MODE'"
        ;;
esac
