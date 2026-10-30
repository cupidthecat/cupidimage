#!/usr/bin/env python3
"""Opt-in PDF corpus audit. Downloaded documents are data, never executable code."""
import argparse
from contextlib import ExitStack
from collections import Counter
import fnmatch
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import tempfile
import time
import sys
from pdf_visuals import save_visuals, write_gallery
import urllib.parse
import urllib.request

from pdf_render_options import add_pdf_options, pdf_option_values, pdf_option_arguments, supports_pdf_options

ROOT = Path(__file__).resolve().parents[1]
MAX_FILE = 64 * 1024 * 1024
MAX_OUTPUT = 300 * 1024 * 1024


def safe_relative(name):
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts or any(p in ('', '.', '..') for p in path.parts) or '\\' in name:
        raise ValueError('Unsafe corpus path: ' + name)
    return Path(*path.parts)


def fetch(row, revision, cache, download):
    relative = safe_relative(row['path'])
    path = cache / revision / relative
    expected_size = row['size']
    if not 0 < expected_size <= MAX_FILE:
        raise ValueError('Source file exceeds download limit')
    if not path.exists():
        if not download:
            raise FileNotFoundError('Not cached; use --download')
        path.parent.mkdir(parents=True, exist_ok=True)
        url = f'https://raw.githubusercontent.com/mozilla/pdf.js/{revision}/test/pdfs/' + urllib.parse.quote(row['path'], safe='/')
        # Never follow .link files, remote PDF actions, or document URLs.
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read(expected_size + 1)
        verify_blob(data, row)
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(data)
        temporary.replace(path)
    if path.stat().st_size != expected_size:
        raise ValueError('Cached size differs from pinned Git tree')
    data = path.read_bytes()
    verify_blob(data, row)
    return path, hashlib.sha256(data).hexdigest()


def verify_blob(data, row):
    if len(data) != row['size']:
        raise ValueError('Downloaded/cached size differs from pinned Git tree')
    digest = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if digest != row['git_blob_sha1']:
        raise ValueError('Downloaded/cached contents differ from pinned Git blob')


def process(command, directory, timeout, memory_mib, label):
    stdout = directory / (label + '.stdout')
    stderr = directory / (label + '.stderr')
    preexec = None
    try:
        import resource
        def limits():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_OUTPUT, MAX_OUTPUT))
            cpu = max(1, math.ceil(timeout))
            resource.setrlimit(resource.RLIMIT_CPU, (cpu, cpu + 1))
            if memory_mib:
                memory = memory_mib * 1024 * 1024
                resource.setrlimit(resource.RLIMIT_AS, (memory, memory))
        preexec = limits
    except ImportError:
        pass
    started = time.monotonic()
    with stdout.open('wb') as out, stderr.open('wb') as err:
        try:
            completed = subprocess.run(command, stdout=out, stderr=err, timeout=timeout, preexec_fn=preexec)
            code = completed.returncode
            status = 'ok' if code == 0 else ('signal' if code < 0 else 'error')
        except subprocess.TimeoutExpired:
            code, status = None, 'timeout'
    with stderr.open('rb') as error_file:
        error = error_file.read(4096).decode('utf-8', errors='replace')
    return {'status': status, 'returncode': code, 'seconds': round(time.monotonic()-started, 3), 'error': error}, stdout


def inspect_output(path):
    with path.open('rb') as source:
        header = source.read(12)
        if len(header) != 12:
            raise ValueError('Truncated probe header')
        width, height, count = struct.unpack('=III', header)
        expected = 12 + width * height * 4
        if not width or not height or expected > MAX_OUTPUT or path.stat().st_size != expected:
            raise ValueError('Invalid probe dimensions or pixel length')
        digest = hashlib.sha256()
        while block := source.read(1024 * 1024): digest.update(block)
    return {'width': width, 'height': height, 'reported_pages': count, 'rgba_sha256': digest.hexdigest()}


def read_ppm(path):
    with path.open('rb') as source:
        tokens = []
        while len(tokens) < 4:
            line = source.readline(4096)
            if not line: raise ValueError('Truncated PPM header')
            tokens.extend(line.split(b'#', 1)[0].split())
        if len(tokens) != 4 or tokens[0] != b'P6' or tokens[3] != b'255':
            raise ValueError('Unsupported PPM layout')
        width, height = map(int, tokens[1:3])
        size = width * height * 3
        if width <= 0 or height <= 0 or size > MAX_OUTPUT: raise ValueError('Oversized reference raster')
        data = source.read(size + 1)
        if len(data) != size: raise ValueError('Reference raster length mismatch')
    return width, height, data


def compare(reference, actual, dimensions):
    width, height, rgb = read_ppm(reference)
    if (width, height) != dimensions:
        return {'status': 'dimension-mismatch', 'reference_width': width, 'reference_height': height,
                'note': 'Page bounds are preserved; no scaling, alignment, or cropping is hidden in this comparison.'}
    with actual.open('rb') as source:
        source.seek(12); rgba = source.read()
    total = changed = maximum = 0
    for i, channel in enumerate(rgb):
        difference = abs(channel - rgba[(i // 3)*4 + i % 3])
        total += difference; maximum = max(maximum, difference); changed += difference != 0
    return {'status': 'exact' if maximum == 0 else 'pixel-difference', 'max_channel_difference': maximum,
            'mean_absolute_error': round(total / len(rgb), 5), 'different_channel_fraction': round(changed / len(rgb), 5)}


def download_only(rows, revision, cache):
    """Fetch and verify inputs without invoking a renderer or changing rating logs."""
    counts = Counter()
    for index, row in enumerate(rows, 1):
        print(f"[{index}/{len(rows)}] {row['path']}", flush=True)
        try:
            cached = (cache / revision / safe_relative(row['path'])).is_file()
            fetch(row, revision, cache, True)
            counts['cached' if cached else 'downloaded'] += 1
        except (OSError, ValueError) as error:
            counts['input-error'] += 1
            print(f"Input unavailable: {error}", file=sys.stderr, flush=True)
    print(json.dumps(dict(sorted(counts.items())), sort_keys=True))
    return 1 if counts['input-error'] else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, default=ROOT / 'bin/test_pdf_pages')
    parser.add_argument('--cache', type=Path, default=ROOT / '.cache/pdfjs-full')
    parser.add_argument('--corpus', type=Path, help='Audit all PDFs in a local corpus instead of downloading the pinned tracked files')
    parser.add_argument('--download', action='store_true', help='Download missing pinned tracked PDFs (about 119 MiB for the complete revision)')
    parser.add_argument('--download-only', action='store_true', help='Download and verify selected pinned PDFs without building, rendering, rating, or changing reports')
    parser.add_argument('--resume', action='store_true', help='Resume a report from the same probe and options; completed documents are skipped')
    parser.add_argument('--match', default='*', help='Glob against relative PDF paths')
    parser.add_argument('--limit', type=int, default=0, help='Limit documents; 0 means every matching document')
    parser.add_argument('--pages', choices=('all', 'first'), default='all')
    parser.add_argument('--max-pages', type=int, default=1000, help='Per-document safety cap; capped pages are reported as untested')
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--memory-mib', type=int, default=1024, help='POSIX address-space cap; use 0 for ASan shadow-memory compatibility')
    parser.add_argument('--fail-on-error', action='store_true', help='Return nonzero for any failed, differing, or untested page')
    parser.add_argument('--reference', action='store_true', help='Compare with installed pdfinfo/pdftoppm at 96 DPI; never installs tools')
    parser.add_argument('--report', type=Path, default=ROOT / '.cache/pdfjs-full/report.jsonl')
    parser.add_argument('--terminal-preview', action='store_true', help='Display generated page comparisons in the terminal with the CLI')
    parser.add_argument('--pause', action='store_true', help='Wait for Enter after each terminal preview')
    parser.add_argument('--cli', type=Path, default=ROOT / 'bin/cupidimage')
    parser.add_argument('--keep-images', action='store_true', help='Retain raw probe output and reference PPMs beside the report')
    add_pdf_options(parser)
    args = parser.parse_args()
    pdf_options = pdf_option_values(args)
    pdf_flags = pdf_option_arguments(pdf_options)
    if args.limit < 0 or args.max_pages < 1 or args.memory_mib < 0 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('Limits must be positive (limit/memory may be zero)')
    if args.download_only and (args.corpus or args.reference or args.terminal_preview or args.pause or args.resume or args.keep_images or pdf_flags):
        parser.error('--download-only cannot be combined with local-corpus, rendering, or resume options')
    if args.pause and (not args.terminal_preview or not sys.stdin.isatty()):
        parser.error('--pause requires --terminal-preview and interactive stdin')
    if args.terminal_preview:
        if not args.cli.is_file(): parser.error('Build the CLI first: make cli')
        args.cli = args.cli.resolve()
        args.keep_images = True
    manifest = json.loads((ROOT / 'tests/fixtures/pdfjs-corpus.json').read_text())
    if args.corpus:
        corpus = args.corpus.resolve()
        rows = [{'path': str(p.relative_to(corpus)), 'local': p} for p in sorted(corpus.rglob('*'))
                if p.is_file() and not p.is_symlink() and p.suffix.lower() == '.pdf']
    else:
        rows = manifest['files']
    rows = [r for r in rows if fnmatch.fnmatchcase(r['path'], args.match)]
    if args.limit: rows = rows[:args.limit]
    if not rows: parser.error("No PDFs match the selected corpus and pattern")
    if args.download_only:
        return download_only(rows, manifest['revision'], args.cache.resolve())
    probe = args.probe.resolve()
    if not probe.is_file(): parser.error('Build the probe first: make bin/test_pdf_pages')
    if args.reference and (not shutil.which('pdfinfo') or not shutil.which('pdftoppm')):
        parser.error('--reference needs installed pdfinfo and pdftoppm')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    counts = Counter()
    configuration = {'revision': None if args.corpus else manifest['revision'],
                     'corpus': str(args.corpus.resolve()) if args.corpus else None,
                     'selection': [row['path'] for row in rows], 'pages': args.pages,
                     'max_pages': args.max_pages, 'timeout_seconds': args.timeout,
                     'memory_mib': args.memory_mib, 'reference_requested': args.reference,
                     'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest()}
    if pdf_flags:
        configuration['pdf_options'] = pdf_options
    completed_files = set()
    if args.resume and args.report.exists():
        previous = [json.loads(line) for line in args.report.read_text().splitlines()]
        if not previous or previous[0].get('configuration') != configuration:
            parser.error('Resume report belongs to a different probe or configuration; use a new report path')
        for record in previous:
            if record.get('kind') == 'completed':
                completed_files.add(record['file']); counts.update(record['counts'])
    mode = 'a' if args.resume and args.report.exists() else 'w'
    with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-run-') as run_directory, ExitStack() as stack:
        snapshot = Path(run_directory) / 'probe'
        shutil.copy2(probe, snapshot)
        probe = snapshot
        if hashlib.sha256(probe.read_bytes()).hexdigest() != configuration['probe_sha256']:
            parser.error('Probe changed while being copied; try again')
        if pdf_flags:
            result, capability = process([str(probe), '--capabilities'], Path(run_directory),
                                         args.timeout, args.memory_mib, 'capabilities')
            if result['status'] != 'ok' or not supports_pdf_options(capability.read_bytes(), pdf_options):
                parser.error('Selected probe cannot confirm the requested PDF options; rebuild bin/test_pdf_pages')
        report = stack.enter_context(args.report.open(mode))
        def emit(row):
            report.write(json.dumps(row, sort_keys=True) + '\n'); report.flush()
        def finish(name, before):
            emit({'kind': 'completed', 'file': name, 'counts': dict(counts - before)})
        if mode == 'w':
            emit({'kind': 'run', 'configuration': configuration,
                  'documents_selected': len(rows),
                  'external_link_files_not_fetched': 0 if args.corpus else len(manifest['external_link_files'])})
        for index, row in enumerate(rows, 1):
            name = row['path']
            if name in completed_files: continue
            before = counts.copy()
            print(f'[{index}/{len(rows)}] {name}', flush=True)
            try:
                if args.corpus:
                    path = row['local']
                    if path.stat().st_size > MAX_FILE: raise ValueError('Local file exceeds size limit')
                    digest = hashlib.sha256(path.read_bytes()).hexdigest()
                else:
                    path, digest = fetch(row, manifest['revision'], args.cache, args.download)
            except Exception as error:
                emit({'kind': 'document', 'file': name, 'status': 'input-error', 'error': str(error)})
                counts['input-error'] += 1
                finish(name, before)
                continue
            with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-audit-') as temporary:
                directory = Path(temporary)
                count_result, count_path = process([str(probe), str(path), 'count'] + pdf_flags, directory, args.timeout, args.memory_mib, 'count')
                page_count = 0
                if count_result['status'] == 'ok':
                    try: page_count = int(count_path.read_text())
                    except ValueError: count_result['status'] = 'invalid-count'
                    if page_count < 1: count_result['status'] = 'invalid-count'
                reference_count = None
                if args.reference:
                    reference_result, info = process(['pdfinfo', str(path)], directory, args.timeout, args.memory_mib, 'info')
                    if reference_result['status'] == 'ok':
                        match = re.search(rb'^Pages:\s+(\d+)', info.read_bytes(), re.MULTILINE)
                        if match: reference_count = int(match[1])
                emit({'kind': 'document', 'file': name, 'sha256': digest, **count_result,
                      'reported_pages': page_count, 'reference_pages': reference_count})
                counts['document-' + count_result['status']] += 1
                if reference_count is not None and reference_count != page_count: counts['page-count-mismatch'] += 1
                wanted = reference_count if reference_count else page_count
                if wanted <= 0:
                    finish(name, before)
                    continue
                tested = min(wanted, args.max_pages, 1 if args.pages == 'first' else wanted)
                if tested < wanted: counts['pages-not-tested'] += wanted - tested
                for page in range(tested):
                    (directory / 'reference.ppm').unlink(missing_ok=True)
                    result, raw = process([str(probe), str(path), str(page)] + pdf_flags, directory, args.timeout, args.memory_mib, 'page')
                    details = {'kind': 'page', 'file': name, 'page': page + 1, **result}
                    if result['status'] == 'ok':
                        try: details.update(inspect_output(raw))
                        except Exception as error: details.update(status='invalid-output', error=str(error))
                    if args.reference:
                        prefix = directory / 'reference'
                        prefix.with_suffix('.ppm').unlink(missing_ok=True)
                        ref, _ = process(['pdftoppm', '-f', str(page+1), '-l', str(page+1), '-singlefile', '-r', '96', '-cropbox', str(path), str(prefix)], directory, args.timeout, args.memory_mib, 'reference-command')
                        if ref['status'] == 'ok':
                            try:
                                details['comparison'] = compare(prefix.with_suffix('.ppm'), raw, (details['width'], details['height'])) if details['status'] == 'ok' else {'status': 'actual-render-failed'}
                            except Exception as error: details['comparison'] = {'status': 'comparison-error', 'error': str(error)}
                        else: details['comparison'] = ref
                        counts['reference-' + details['comparison']['status']] += 1
                    if args.keep_images:
                        target = args.report.parent / (args.report.stem + '-images') / f'{index:04}-{page+1:04}'
                        target.mkdir(parents=True, exist_ok=True)
                        for artifact in ('actual.rgba', 'reference.ppm', 'actual.png', 'reference.png',
                                         'actual-preview.png', 'reference-preview.png', 'comparison.png', 'difference.png'):
                            (target / artifact).unlink(missing_ok=True)
                        shutil.copyfile(raw, target / 'actual.rgba')
                        ppm = directory / 'reference.ppm'
                        if args.reference and ppm.exists(): shutil.copyfile(ppm, target / 'reference.ppm')
                        details['artifacts'] = str(target)
                        try:
                            save_visuals(target, raw if details['status'] == 'ok' else None,
                                         ppm if args.reference and ppm.exists() else None, read_ppm)
                        except Exception as error:
                            details['artifact_error'] = str(error); counts['artifact-error'] += 1
                    emit(details); counts['page-' + details['status']] += 1
                    if args.terminal_preview and args.keep_images:
                        preview = target / 'comparison.png'
                        label = 'left: cupidimage / right: reference'
                        if not preview.exists():
                            preview = target / 'actual-preview.png'; label = 'cupidimage output'
                        if not preview.exists():
                            preview = target / 'reference-preview.png'; label = 'reference only; actual rendering failed'
                        if preview.exists():
                            print(f"{name}, page {page+1}: {label}", flush=True)
                            try: subprocess.run([str(args.cli), '--fit', str(preview)], timeout=args.timeout, check=False)
                            except subprocess.TimeoutExpired: print('Terminal preview timed out', flush=True)
                        else: print(details.get('error') or 'No page image available', flush=True)
                        if args.pause: input('Enter for next page; Ctrl+C to stop: ')
            finish(name, before)
        emit({'kind': 'summary', 'counts': dict(counts)})
    print(json.dumps(dict(counts), sort_keys=True))
    print('Report:', args.report)
    if args.keep_images:
        print('Visual gallery:', write_gallery(args.report))
    # This is a diagnostic audit, not a conformance claim or a normal CI gate.
    passed = {'document-ok', 'page-ok', 'reference-exact'}
    return int(args.fail_on_error and any(value for key, value in counts.items() if key not in passed))


if __name__ == '__main__':
    raise SystemExit(main())
