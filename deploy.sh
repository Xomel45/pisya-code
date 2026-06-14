#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# deploy.sh — упаковка и развёртывание артефактов Pisya Code
# ══════════════════════════════════════════════════════════════════════════════
#
# Использование:
#   ./deploy.sh beta                  — сырой бинарь → builds/beta/pisya
#   ./deploy.sh release                — .tar.gz (универсальный) → builds/releases/
#   ./deploy.sh release tar            — то же самое явно
#   ./deploy.sh release pkg/arch       — .pkg.tar.zst (Arch/pacman)
#   ./deploy.sh release deb/debian     — .deb (Debian/Ubuntu/Mint)
#   ./deploy.sh release rpm/rh         — .rpm (Fedora/RHEL/openSUSE)
#   ./deploy.sh release my             — авто: пакет под текущий дистрибутив
#   ./deploy.sh release all            — tar.gz + pkg + deb + rpm (что доступно)
#
# Опции:
#   --build     Пересобрать проект перед деплоем (Release, CMake)
#   --clean     Удалить целевую директорию перед копированием
#
# Примеры:
#   ./deploy.sh beta --build              Собрать и задеплоить сырой бинарь
#   ./deploy.sh release --build           Собрать и упаковать .tar.gz
#   ./deploy.sh release my --build        Собрать и упаковать под свой дистрибутив
#   ./deploy.sh release all --build --clean
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

# ── Константы ─────────────────────────────────────────────────────────────────
APP_NAME="pisya"
PKG_DESC="Local AI coding assistant (C++23, OpenAI-compatible API)"
PKG_URL="https://github.com/Xomel45/pisya-code"
PKG_LICENSE="MPL-2.0"
MAINTAINER="Xomel45 <xom.xom.zip@gmail.com>"

BUILD_DIR="build-release"   # отдельная от дев-сборки (build/) директория
BUILDS_DIR="builds"         # корень артефактов (не коммитится)
ARCH="$(uname -m)"          # x86_64 / aarch64 / ...

# ── Версия из CMakeLists.txt ─────────────────────────────────────────────────
# Берём из строки вида: project(pisya-code VERSION 0.1.0 LANGUAGES CXX)
get_version() {
    local v
    v=$(sed -n 's/^project([^ ]* VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
    echo "${v:-unknown}"
}
VERSION=$(get_version)

# ── Парсинг аргументов ────────────────────────────────────────────────────────
MODE="${1:-help}"
PLATFORM="${2:-tar}"
DO_BUILD=false
DO_CLEAN=false

for arg in "$@"; do
    [[ "$arg" == "--build" ]] && DO_BUILD=true
    [[ "$arg" == "--clean" ]] && DO_CLEAN=true
done

# ── Вспомогательные функции ───────────────────────────────────────────────────

# Размер файла в человекочитаемом виде
file_size() { du -h "$1" 2>/dev/null | cut -f1 || echo "?"; }

# Короткий git-хеш текущего коммита
git_hash() { git rev-parse --short HEAD 2>/dev/null || echo "unknown"; }

# debian-имя архитектуры (amd64/arm64) из uname -m (x86_64/aarch64)
deb_arch() {
    case "$ARCH" in
        x86_64)  echo "amd64" ;;
        aarch64) echo "arm64" ;;
        *)       echo "$ARCH" ;;
    esac
}

# Сохраняем метаданные сборки рядом с артефактом
write_build_info() {
    local dir="$1"
    printf "version=%s\nbuilt=%s\ncommit=%s\narch=%s\n" \
        "$VERSION" \
        "$(date '+%Y-%m-%d %H:%M:%S')" \
        "$(git_hash)" \
        "$ARCH" \
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

    local arch_re='arch|manjaro|endeavouros|garuda|artix|cachyos|blackarch'
    local deb_re='debian|ubuntu|linuxmint|raspbian|pop|kali|elementary|zorin|mx|devuan'
    local rpm_re='fedora|rhel|centos|rocky|almalinux|opensuse|suse|oracle|mageia|nobara'

    if   echo "$combined" | grep -qiE "$arch_re"; then echo "pkg"
    elif echo "$combined" | grep -qiE "$deb_re";  then echo "deb"
    elif echo "$combined" | grep -qiE "$rpm_re";  then echo "rpm"
    else echo "unknown"
    fi
}

# Создаём builds/ если её нет (не добавлять в git!)
ensure_builds_tree() {
    mkdir -p "$BUILDS_DIR/beta"
    mkdir -p "$BUILDS_DIR/releases"
}

# Безопасное удаление — только внутри BUILDS_DIR.
# Защищает от path-traversal (например, через странную VERSION).
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

# ── Сборка ────────────────────────────────────────────────────────────────────

build_pisya() {
    header "Сборка Release"
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -Wno-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF
    cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
    ok "Бинарь собран: $BUILD_DIR/$APP_NAME"
}

ensure_binary() {
    if $DO_BUILD || [[ ! -f "$BUILD_DIR/$APP_NAME" ]]; then
        build_pisya
    fi
}

# ── РЕЖИМ: Beta ───────────────────────────────────────────────────────────────
# Сырой бинарь, без упаковки — для быстрой проверки на этой же машине.
deploy_beta() {
    header "Beta Deploy → ${BUILDS_DIR}/beta/"
    ensure_builds_tree
    ensure_binary

    local dest="${BUILDS_DIR}/beta"
    $DO_CLEAN && safe_clean "$dest"
    mkdir -p "$dest"

    cp "$BUILD_DIR/$APP_NAME" "$dest/$APP_NAME"
    chmod +x "$dest/$APP_NAME"
    write_build_info "$dest"

    rule
    ok "Beta готов!"
    echo -e "  Путь:    ${BOLD}$dest/$APP_NAME${NC}"
    echo "  Размер:  $(file_size "$dest/$APP_NAME")"
    echo "  Версия:  $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Запуск:"
    echo "    ./$dest/$APP_NAME"
    echo ""
}

# ── РЕЖИМ: Release .tar.gz (универсальный) ──────────────────────────────────
deploy_release_tar() {
    header "Release .tar.gz → ${BUILDS_DIR}/releases/${VERSION}-linux/"
    ensure_builds_tree
    ensure_binary

    local dest="${BUILDS_DIR}/releases/${VERSION}-linux"
    local pkg_name="pisya-${VERSION}-${ARCH}.tar.gz"
    local staging="${BUILD_DIR}/tar-staging/pisya-${VERSION}-${ARCH}"

    $DO_CLEAN && safe_clean "$dest"
    mkdir -p "$dest"

    local dest_abs
    dest_abs="$(realpath -m "$dest")"

    rm -rf "$(dirname "$staging")"
    mkdir -p "$staging"
    cp "$BUILD_DIR/$APP_NAME" "$staging/"
    chmod +x "$staging/$APP_NAME"
    cp README.md LICENSE "$staging/"
    ok "  + $APP_NAME, README.md, LICENSE"

    (cd "$(dirname "$staging")" && tar czf "$dest_abs/$pkg_name" "$(basename "$staging")")
    rm -rf "$(dirname "$staging")"

    write_build_info "$dest"

    rule
    ok "Release .tar.gz готов!"
    echo -e "  Директория:  ${BOLD}$dest/${NC}"
    echo "  Архив:       $pkg_name  ($(file_size "$dest/$pkg_name"))"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Установка:"
    echo "    tar xzf $pkg_name && sudo install -m 755 pisya-${VERSION}-${ARCH}/pisya /usr/local/bin/pisya"
    echo ""
}

# ── РЕЖИМ: Release .pkg.tar.zst (Arch Linux) ─────────────────────────────────
deploy_release_pkg() {
    header "Release .pkg.tar.zst → ${BUILDS_DIR}/releases/${VERSION}-linux/"
    ensure_builds_tree

    if ! command -v zstd &>/dev/null; then
        fail "zstd не найден. Установите: sudo pacman -S zstd"
    fi

    ensure_binary

    local dest="${BUILDS_DIR}/releases/${VERSION}-linux"
    local pkgver="${VERSION}-1"
    local pkg_name="${APP_NAME}-${ARCH}.pkg.tar.zst"
    local staging="${BUILD_DIR}/pkg-staging"

    $DO_CLEAN && safe_clean "$dest"
    mkdir -p "$dest"

    rm -rf "$staging"
    mkdir -p "$staging/usr/bin"
    mkdir -p "$staging/usr/share/licenses/${APP_NAME}"
    mkdir -p "$staging/usr/share/doc/${APP_NAME}"

    cp "$BUILD_DIR/$APP_NAME" "$staging/usr/bin/"
    chmod 755 "$staging/usr/bin/$APP_NAME"
    ok "  + usr/bin/$APP_NAME"

    cp LICENSE "$staging/usr/share/licenses/${APP_NAME}/"
    cp README.md "$staging/usr/share/doc/${APP_NAME}/"
    ok "  + usr/share/licenses/${APP_NAME}/, usr/share/doc/${APP_NAME}/"

    local installed_size
    installed_size=$(du -sb "$staging" | cut -f1)

    cat > "$staging/.PKGINFO" << PKGINFO_EOF
pkgname = ${APP_NAME}
pkgver = ${pkgver}
pkgdesc = ${PKG_DESC}
url = ${PKG_URL}
builddate = $(date +%s)
packager = ${MAINTAINER}
size = ${installed_size}
arch = ${ARCH}
license = ${PKG_LICENSE}
depend = openssl
depend = gcc-libs
PKGINFO_EOF
    ok "  + .PKGINFO  (depends: openssl, gcc-libs)"

    tar --zstd -cf "$dest/$pkg_name" -C "$staging" .PKGINFO usr
    rm -rf "$staging"

    rule
    ok "Release .pkg.tar.zst готов!"
    echo -e "  Директория:  ${BOLD}$dest/${NC}"
    echo "  Пакет:       $pkg_name"
    echo "  Размер:      $(file_size "$dest/$pkg_name")"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Установка:"
    echo "    sudo pacman -U $dest/$pkg_name"
    echo ""
}

# ── РЕЖИМ: Release .deb (Debian/Ubuntu/Mint) ─────────────────────────────────
deploy_release_deb() {
    header "Release .deb → ${BUILDS_DIR}/releases/${VERSION}-linux/"
    ensure_builds_tree

    if ! command -v dpkg-deb &>/dev/null; then
        fail "dpkg-deb не найден. Установите: sudo apt install dpkg-dev  /  sudo pacman -S dpkg"
    fi

    ensure_binary

    local dest="${BUILDS_DIR}/releases/${VERSION}-linux"
    local deb_name="${APP_NAME}_${VERSION}_$(deb_arch).deb"
    local pkg_dir="${BUILD_DIR}/deb-staging"

    $DO_CLEAN && safe_clean "$dest"
    mkdir -p "$dest"

    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir/DEBIAN"
    mkdir -p "$pkg_dir/usr/bin"
    mkdir -p "$pkg_dir/usr/share/doc/${APP_NAME}"

    local installed_kb
    installed_kb=$(du -sk "$BUILD_DIR/$APP_NAME" | cut -f1)

    cat > "$pkg_dir/DEBIAN/control" << CTRL_EOF
Package: ${APP_NAME}
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: $(deb_arch)
Installed-Size: ${installed_kb}
Depends: libssl3 | libssl1.1, libstdc++6, libc6
Maintainer: ${MAINTAINER}
Homepage: ${PKG_URL}
Description: ${PKG_DESC}
 Pisya Code connects to any OpenAI-compatible model (Ollama, LM Studio, etc.)
 and edits your files directly from the terminal — like Claude Code, but
 fully offline-capable.
CTRL_EOF

    cp "$BUILD_DIR/$APP_NAME" "$pkg_dir/usr/bin/"
    chmod 755 "$pkg_dir/usr/bin/$APP_NAME"
    ok "  + usr/bin/$APP_NAME"

    cp LICENSE "$pkg_dir/usr/share/doc/${APP_NAME}/copyright"
    cp README.md "$pkg_dir/usr/share/doc/${APP_NAME}/"
    ok "  + usr/share/doc/${APP_NAME}/"

    find "$pkg_dir/usr/share" -type f -exec chmod 644 {} \;
    find "$pkg_dir" -type d -exec chmod 755 {} \;

    dpkg-deb --build --root-owner-group "$pkg_dir" "$dest/$deb_name"
    rm -rf "$pkg_dir"

    rule
    ok "Release .deb готов!"
    echo -e "  Директория:  ${BOLD}$dest/${NC}"
    echo "  Пакет:       $deb_name"
    echo "  Размер:      $(file_size "$dest/$deb_name")"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Установка:"
    echo "    sudo dpkg -i $deb_name"
    echo "    sudo apt-get install -f   # если нужно подтянуть зависимости"
    echo ""
}

# ── РЕЖИМ: Release .rpm (Fedora/RHEL/openSUSE) ───────────────────────────────
deploy_release_rpm() {
    header "Release .rpm → ${BUILDS_DIR}/releases/${VERSION}-linux/"
    ensure_builds_tree

    if ! command -v rpmbuild &>/dev/null; then
        fail "rpmbuild не найден. Установите: sudo dnf install rpm-build  /  sudo pacman -S rpm-tools"
    fi

    ensure_binary

    local dest="${BUILDS_DIR}/releases/${VERSION}-linux"
    local rpm_version="${VERSION//-/_}"
    local rpm_name="${APP_NAME}-${ARCH}.rpm"
    local rpm_topdir="${BUILD_DIR}/rpm-staging"
    local buildroot="${rpm_topdir}/BUILDROOT/${APP_NAME}-${rpm_version}-1.${ARCH}"

    $DO_CLEAN && safe_clean "$dest"
    mkdir -p "$dest"

    rm -rf "$rpm_topdir"
    mkdir -p "${rpm_topdir}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    mkdir -p "$buildroot/usr/bin"
    mkdir -p "$buildroot/usr/share/doc/${APP_NAME}"
    mkdir -p "$buildroot/usr/share/licenses/${APP_NAME}"

    cp "$BUILD_DIR/$APP_NAME" "$buildroot/usr/bin/"
    chmod 755 "$buildroot/usr/bin/$APP_NAME"
    ok "  + usr/bin/$APP_NAME"

    cp README.md "$buildroot/usr/share/doc/${APP_NAME}/"
    cp LICENSE "$buildroot/usr/share/licenses/${APP_NAME}/"
    ok "  + usr/share/doc/${APP_NAME}/, usr/share/licenses/${APP_NAME}/"

    local files_list
    files_list=$(find "$buildroot" \( -type f -o -type l \) | sed "s|${buildroot}||" | sort)

    local changelog_date
    changelog_date=$(LC_ALL=C date '+%a %b %d %Y')

    cat > "${rpm_topdir}/SPECS/${APP_NAME}.spec" << SPEC_EOF
Name:           ${APP_NAME}
Version:        ${rpm_version}
Release:        1
Summary:        ${PKG_DESC}
License:        ${PKG_LICENSE}
URL:            ${PKG_URL}
BuildArch:      ${ARCH}
Requires:       openssl-libs, libstdc++
%define __spec_install_pre %{nil}
%define _unpackaged_files_terminate_build 0

%description
Pisya Code connects to any OpenAI-compatible model (Ollama, LM Studio, etc.)
and edits your files directly from the terminal — like Claude Code, but
fully offline-capable.

%build
# pre-built binary

%install
# buildroot already populated externally

%files
$(echo "$files_list")

%changelog
* ${changelog_date} ${MAINTAINER} - ${rpm_version}-1
- Release ${VERSION}
SPEC_EOF

    rpmbuild -bb \
        --nodeps \
        --define "_topdir $(realpath "$rpm_topdir")" \
        --buildroot "$(realpath "$buildroot")" \
        "${rpm_topdir}/SPECS/${APP_NAME}.spec"

    local created_rpm
    created_rpm=$(find "${rpm_topdir}/RPMS" -name "*.rpm" | head -1 || true)

    if [[ -z "$created_rpm" || ! -f "$created_rpm" ]]; then
        fail ".rpm не создан! Проверь вывод rpmbuild выше."
    fi

    cp "$created_rpm" "$dest/$rpm_name"
    rm -rf "$rpm_topdir"

    rule
    ok "Release .rpm готов!"
    echo -e "  Директория:  ${BOLD}$dest/${NC}"
    echo "  Пакет:       $rpm_name"
    echo "  Размер:      $(file_size "$dest/$rpm_name")"
    echo "  Версия:      $VERSION  |  commit: $(git_hash)"
    echo ""
    echo "  Установка:"
    echo "    sudo rpm -i $rpm_name                 # RPM-based"
    echo "    sudo dnf install ./$rpm_name          # Fedora / RHEL"
    echo ""
}

# ── Вывод помощи ─────────────────────────────────────────────────────────────
show_help() {
    echo ""
    echo -e "${BOLD}${CYAN}deploy.sh${NC} — упаковщик артефактов Pisya Code v${BOLD}${VERSION}${NC}"
    rule
    echo ""
    echo -e "  ${BOLD}Использование:${NC}"
    echo "    ./deploy.sh beta                 Сырой бинарь → builds/beta/pisya"
    echo "    ./deploy.sh release               .tar.gz (универсальный) → builds/releases/${VERSION}-linux/"
    echo "    ./deploy.sh release tar           То же явно"
    echo "    ./deploy.sh release pkg/arch      .pkg.tar.zst (Arch/pacman)"
    echo "    ./deploy.sh release deb/debian    .deb (Debian/Ubuntu/Mint)"
    echo "    ./deploy.sh release rpm/rh        .rpm (Fedora/RHEL/openSUSE)"
    echo "    ./deploy.sh release my            Авто: пакет под текущий дистрибутив"
    echo "    ./deploy.sh release all           Всё: tar.gz + pkg + deb + rpm (что доступно)"
    echo ""
    echo -e "  ${BOLD}Опции:${NC}"
    echo "    --build     Пересобрать перед деплоем (CMake Release)"
    echo "    --clean     Удалить целевую директорию перед копированием"
    echo ""
    echo -e "  ${BOLD}Примеры:${NC}"
    echo "    ./deploy.sh beta --build"
    echo "    ./deploy.sh release --build              # .tar.gz"
    echo "    ./deploy.sh release my --build           # под свой дистрибутив"
    echo "    ./deploy.sh release all --build --clean  # всё разом"
    echo ""
    echo -e "  ${BOLD}Требования для пакетов:${NC}"
    echo "    .pkg.tar.zst   — zstd     (sudo pacman -S zstd)"
    echo "    .deb           — dpkg-deb (sudo apt install dpkg-dev / sudo pacman -S dpkg)"
    echo "    .rpm           — rpmbuild (sudo dnf install rpm-build / sudo pacman -S rpm-tools)"
    echo ""
    echo -e "  ${BOLD}Структура вывода:${NC}"
    echo "    builds/"
    echo "    ├── beta/"
    echo "    │   ├── pisya"
    echo "    │   └── build-info.txt"
    echo "    └── releases/"
    echo "        └── ${VERSION}-linux/"
    echo "            ├── pisya-${VERSION}-${ARCH}.tar.gz"
    echo "            ├── pisya-${ARCH}.pkg.tar.zst"
    echo "            ├── pisya_${VERSION}_$(deb_arch).deb"
    echo "            ├── pisya-${ARCH}.rpm"
    echo "            └── build-info.txt"
    echo ""
    echo -e "  ${YELLOW}Для локальной установки на эту же машину используй ./install.sh${NC}"
    echo ""
}

# ── Точка входа ───────────────────────────────────────────────────────────────
case "$MODE" in
    beta)
        deploy_beta
        ;;
    release)
        case "$PLATFORM" in
            tar|--build|--clean|"")
                deploy_release_tar
                ;;
            pkg|arch|Arch)
                deploy_release_pkg
                ;;
            deb|debian|Debian)
                deploy_release_deb
                ;;
            rpm|rh|RH|red-hat|Red-Hat)
                deploy_release_rpm
                ;;
            my)
                fmt=$(detect_pkg_format)
                case "$fmt" in
                    pkg) log "Определён дистрибутив: Arch-семейство → .pkg.tar.zst"; deploy_release_pkg ;;
                    deb) log "Определён дистрибутив: Debian-семейство → .deb";        deploy_release_deb ;;
                    rpm) log "Определён дистрибутив: RPM-семейство → .rpm";           deploy_release_rpm ;;
                    *)   warn "Не удалось определить дистрибутив — собираю универсальный .tar.gz"; deploy_release_tar ;;
                esac
                ;;
            all)
                if $DO_CLEAN; then
                    ensure_builds_tree
                    safe_clean "${BUILDS_DIR}/releases/${VERSION}-linux"
                    DO_CLEAN=false
                fi
                deploy_release_tar
                command -v zstd      &>/dev/null && deploy_release_pkg || warn "zstd не найден — .pkg.tar.zst пропущен"
                command -v dpkg-deb  &>/dev/null && deploy_release_deb || warn "dpkg-deb не найден — .deb пропущен"
                command -v rpmbuild  &>/dev/null && deploy_release_rpm || warn "rpmbuild не найден — .rpm пропущен"
                ;;
            *)
                fail "Неизвестный формат: '$PLATFORM'. Доступно: tar, pkg, deb, rpm, my, all"
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
