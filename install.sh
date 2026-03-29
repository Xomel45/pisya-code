#!/usr/bin/env bash
set -e

# ── защита от дегенератов ─────────────────────────────────────────────────────
if [ "$(id -u)" -eq 0 ]; then
    echo ""
    echo "  ✗ Не запускай install.sh от root / sudo."
    echo "  Скрипт сам попросит пароль там, где нужно."
    echo ""
    exit 1
fi

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_DIR/build"
BINARY="$BUILD_DIR/pisya"
INSTALL_DIR="/usr/local/bin"

echo ""
echo "  ┌─────────────────────────────────────┐"
echo "  │        Pisya Code  installer        │"
echo "  └─────────────────────────────────────┘"
echo ""

# ── проверка зависимостей ─────────────────────────────────────────────────────
need() {
    if ! command -v "$1" &>/dev/null; then
        echo "  ✗ Не найден: $1 — установи и попробуй снова."
        exit 1
    fi
}
need cmake
need git

# Проверяем компилятор (g++ или clang++)
if command -v g++ &>/dev/null; then
    CXX_OK=1
elif command -v clang++ &>/dev/null; then
    CXX_OK=1
else
    echo "  ✗ Не найден C++ компилятор (g++ или clang++) — установи и попробуй снова."
    exit 1
fi

# ── сборка ────────────────────────────────────────────────────────────────────
echo "  › Конфигурация..."
cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -Wno-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF 2>&1 \
    | grep -v "^--" || true

echo "  › Сборка..."
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

# ── установка ─────────────────────────────────────────────────────────────────
echo "  › Установка в $INSTALL_DIR (нужен sudo)..."
sudo install -m 755 "$BINARY" "$INSTALL_DIR/pisya"

# ── проверка ──────────────────────────────────────────────────────────────────
echo ""
if command -v pisya &>/dev/null; then
    echo "  ✓ Готово! Теперь из любой директории:"
    echo ""
    echo "      pisya"
    echo ""
else
    echo "  ✓ Бинарь установлен в $INSTALL_DIR/pisya"
    echo "  ⚠ Убедись, что $INSTALL_DIR есть в \$PATH."
    echo ""
fi
