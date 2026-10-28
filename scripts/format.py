#!/usr/bin/env python3
"""Apply or check the repository's pinned C/C++ formatting style."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {'.c', '.h', '.cc', '.hh', '.cpp', '.hpp'}


def source_files():
    output = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT)
    files = []
    for raw in output.split(b'\0'):
        if not raw:
            continue
        relative = Path(os.fsdecode(raw))
        if relative.parts[0] == 'third_party' or relative.suffix not in SUFFIXES:
            continue
        path = ROOT / relative
        if path.is_symlink() or not path.is_file():
            continue
        files.append(str(relative))
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--write', action='store_true', help='Format tracked project C/C++ files')
    mode.add_argument('--check', action='store_true', help='Check without changing files (default)')
    args = parser.parse_args()
    executable = os.environ.get('CLANG_FORMAT', 'clang-format')
    expected = (ROOT / '.clang-format-version').read_text().strip()
    try:
        version = subprocess.check_output([executable, '--version'], text=True).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        print(f'Cannot run clang-format: {error}', file=sys.stderr)
        print(f'Install clang-format {expected}, or set CLANG_FORMAT to its executable.', file=sys.stderr)
        return 1
    match = re.search(r'version\s+(\d+\.\d+\.\d+)', version)
    if not match or match.group(1) != expected:
        print(f'Expected clang-format {expected}; found {version}', file=sys.stderr)
        return 1
    files = source_files()
    if not files:
        print('No tracked C/C++ source files found.')
        return 0
    command = [executable, '--style=file', '--fallback-style=none']
    command += ['-i'] if args.write else ['--dry-run', '--Werror']
    result = subprocess.run(command + files, cwd=ROOT)
    if result.returncode == 0:
        action = 'Formatted' if args.write else 'Checked'
        print(f'{action} {len(files)} project source files with clang-format {expected}.')
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
