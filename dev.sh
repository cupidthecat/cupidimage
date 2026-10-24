#!/bin/bash
# Development build script with strict safety and debugging flags
# This compiles everything with maximum warnings, error checking, and sanitizers

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== CupidImage Development Build ===${NC}"
echo "Compiling with strict safety flags, sanitizers, and debug symbols..."

# Compiler settings
CC="${CC:-gcc}"
AR="${AR:-ar}"

# Strict development flags
DEV_CFLAGS="-std=c99 -g3 -O0 \
-Wall -Wextra -Werror \
-Wpedantic \
-Wformat=2 \
-Wformat-security \
-Wnull-dereference \
-Wstack-protector \
-Wtrampolines \
-Warray-bounds \
-Wcast-align \
-Wcast-qual \
-Wconversion \
-Wsign-conversion \
-Wstrict-overflow=4 \
-Wundef \
-Wunused \
-Wuninitialized \
-Wshadow \
-Wpointer-arith \
-Wwrite-strings \
-Wswitch-default \
-Wswitch-enum \
-Wmissing-declarations \
-Wmissing-prototypes \
-Wstrict-prototypes \
-Wredundant-decls \
-Wdouble-promotion \
-Wfloat-equal \
-Wvla \
-fstack-protector-strong \
-fno-omit-frame-pointer \
-D_FORTIFY_SOURCE=2"

# Sanitizer flags (detect memory errors, undefined behavior, etc.)
SANITIZER_FLAGS="-fsanitize=address -fsanitize=undefined -fsanitize=leak -fsanitize=bounds -fsanitize=null -fsanitize=return"

# Combined flags
CFLAGS="${DEV_CFLAGS} ${SANITIZER_FLAGS}"
LDLIBS="-lm"
INCLUDES="-Isrc -I."

# Use the same source discovery as normal builds, in separate output directories.
exec make -j2 all CC="$CC" AR="$AR" CFLAGS="$CFLAGS" LDLIBS="$LDLIBS" \
    OBJDIR="${OBJDIR:-obj/dev}" BINDIR="${BINDIR:-bin/dev}"
