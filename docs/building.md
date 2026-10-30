# Build and installation

## Requirements

The library uses C99, the C standard library, and the math library (`-lm` on
systems that require it). It does not link external PNG, JPEG, WebP, or PDF
libraries. The normal build uses GNU Make and a compiler such as GCC or Clang.
The CLI currently uses POSIX facilities including `unistd.h`, `ioctl`, and
`nanosleep`; native Windows CLI support is not established.

```sh
make -j2              # static library and CLI
make lib              # bin/libcupidimage.a
make cli              # bin/cupidimage
scripts/make.sh        # thin wrapper around make
```

Compile flags are configurable:

```sh
make CC=clang CFLAGS='-O2 -Wall -Wextra -std=c99'
```

Use distinct output directories when changing compiler or instrumentation, or
clean first. Make does not automatically rebuild objects solely because a flag
value changed.

```sh
make -j2 CC=clang OBJDIR=obj/clang BINDIR=bin/clang \
  CFLAGS='-O1 -g -Wall -Wextra -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer'
```

Python 3 and Bash are needed for the regression suite. Optional asset generators
have their own dependencies; routine decoding and tests use committed fixtures.

## Source formatting

Project C and C++ files use the LLVM-based settings in `.clang-format`.
The formatter version is pinned in `.clang-format-version` so local edits and CI
produce the same result. This is a development tool, not a library dependency.

Install it in a virtual environment:

```sh
python3 -m venv .cache/format-venv
.cache/format-venv/bin/python -m pip install "clang-format==$(cat .clang-format-version)"
make format CLANG_FORMAT="$PWD/.cache/format-venv/bin/clang-format"
make format-check CLANG_FORMAT="$PWD/.cache/format-venv/bin/clang-format"
```

If the pinned `clang-format` is already on `PATH`, omit `CLANG_FORMAT`.
`make format` rewrites tracked project sources; `make format-check` checks them
without changing files. Add new source files to Git before running either target.
Vendored files under `third_party/` and generated `.inc` tables stay unchanged.
CI runs the check on pushes and pull requests.

## Install the library

The install script installs the static library and public header, not the CLI:

```sh
PREFIX="$HOME/.local" scripts/install.sh
PREFIX="$HOME/.local" scripts/install.sh --uninstall
```

It defaults to `/usr/local`, which may require elevated permissions. The script
builds the library before installation. An embedding program can link it with:

```sh
cc -I"$HOME/.local/include" app.c -L"$HOME/.local/lib" -lcupidimage -lm -o app
```

For an in-tree example:

```sh
cc -Isrc examples/preview.c bin/libcupidimage.a -lm -o bin/preview
```

## Clean and debug

`make clean` removes the normal objects, archive, CLI, and listed test binaries.
Pass the same `OBJDIR` and `BINDIR` to clean a separate build. Cached external
corpora are not removed by `make clean`.

Decoder debug macros include `CUPIDIMAGE_JPEG_DEBUG`, `CUPIDIMAGE_BMP_DEBUG`,
`CUPIDIMAGE_TIFF_DEBUG`, `CUPIDIMAGE_ICO_DEBUG`, and `CUPIDIMAGE_TGA_DEBUG`.
Add the selected macro to `CFLAGS` for a separate diagnostic build. Keep debug
output out of reference-pixel test streams.
