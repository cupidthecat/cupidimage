#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
cd "$ROOT_DIR"
CC="${CC:-cc}"
read -r -a flags <<< "${CFLAGS:--O2 -g -Wall -Wextra -std=c99}"
"$CC" "${flags[@]}" -Isrc -Dmalloc=gif_test_malloc -Drealloc=gif_test_realloc \
    -Dfree=gif_test_free -c src/formats/gif/cupidimage_gif.c -o "$BUILD_DIR/gif.o"
"$CC" "${flags[@]}" -Isrc tests/gif_regressions.c src/core/io.c \
    "$BUILD_DIR/gif.o" -o "$BUILD_DIR/test_gif"
"$BUILD_DIR/test_gif" "$@"
