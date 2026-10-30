#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
cd "$ROOT_DIR"
read -r -a flags <<< "${CFLAGS:--O2 -g -Wall -Wextra -std=c99}"
"${CC:-cc}" "${flags[@]}" -Isrc scripts/test_terminal_api.c src/core/image.c src/output/ansi/ansi.c \
    src/core/io.c src/compression/zlib.c src/output/kitty/cupidimage_kitty.c -o "$BUILD_DIR/test_terminal"
"$BUILD_DIR/test_terminal"
python3 scripts/test_terminal_api.py "$BUILD_DIR/test_terminal"
