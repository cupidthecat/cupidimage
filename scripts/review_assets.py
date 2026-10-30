#!/usr/bin/env python3
"""Interactively render selected assets and save human visual-review ratings."""
import argparse
from collections import Counter
from contextlib import ExitStack
from datetime import datetime, timezone
import fnmatch
import glob
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from audit_pdf_corpus import fetch, process, read_ppm
from pdf_visuals import save_visuals, write_png
from test_asset_corpus import bundle
from test_assets import SUFFIXES
from pdf_render_options import add_pdf_options, pdf_option_values, pdf_option_arguments, supports_pdf_options, pdf_option_key_values

ROOT = Path(__file__).resolve().parents[1]
RATINGS = {'c': 'correct', 'correct': 'correct', '1': 'correct',
           'p': 'partial', 'partial': 'partial', 'partly wrong': 'partial', 'half bad': 'partial', '2': 'partial',
           'w': 'wrong', 'wrong': 'wrong', '3': 'wrong',
           's': 'skip', 'skip': 'skip', '0': 'skip'}


def select_range(value, total):
    if value == 'all': return list(range(total))
    if value == 'first': return [0] if total else []
    selected = set()
    for part in value.split(','):
        pieces = part.strip().split('-')
        if len(pieces) not in (1, 2) or any(not p.isdigit() for p in pieces):
            raise ValueError('Use 1-based ranges such as 1-5,8,10-12')
        first = int(pieces[0]); last = int(pieces[-1])
        if first < 1 or last < first: raise ValueError('Ranges must be positive and increasing')
        selected.update(range(first-1, min(last, total)))
    return sorted(selected)


def label(text):
    return ''.join(c if ord(c) >= 32 and ord(c) != 127 else f'\\x{ord(c):02x}' for c in str(text))


def collect(paths, assets=False, formats=(), pattern='*', include_negative=False, pdfjs=False):
    items = {}
    sources = [str(ROOT/'assets')] if assets or (not paths and not pdfjs) else []
    sources += paths
    for source in sources:
        matches = glob.glob(source, recursive=True)
        if not matches: raise ValueError('No path matches: '+source)
        for match in matches:
            path = Path(match)
            candidates = path.rglob('*') if path.is_dir() else [path]
            for candidate in candidates:
                suffix = candidate.suffix.lower()
                if not candidate.is_file() or candidate.is_symlink() or suffix not in SUFFIXES: continue
                if not include_negative and 'negative' in candidate.parts: continue
                absolute = candidate.resolve()
                items[str(absolute)] = {'name': str(absolute), 'path': absolute, 'format': SUFFIXES[suffix]}
    manifest = json.loads((ROOT/'tests/fixtures/pdfjs-corpus.json').read_text())
    if pdfjs:
        for row in manifest['files']:
            items['pdfjs:'+row['path']] = {'name': 'pdfjs/'+row['path'], 'format': 'pdf', 'remote': row,
                                          'revision': manifest['revision']}
    wanted = {part.strip().lower() for value in formats for part in value.split(',')}
    wanted = {'jpeg' if f in ('jpg', 'jpeg') else 'tiff' if f in ('tif', 'tiff') else f for f in wanted}
    unknown = wanted - set(SUFFIXES.values())
    if unknown: raise ValueError('Unknown formats: '+', '.join(sorted(unknown)))
    return [row for _,row in sorted(items.items()) if (not wanted or row['format'] in wanted) and
            (fnmatch.fnmatchcase(row['name'], pattern) or fnmatch.fnmatchcase(Path(row['name']).name, pattern))]


def review_key(path, digest, page, probe_hash, cli_hash, backend, reference=False, reviewer="local reviewer", pdf_options=None):
    values = [str(path), digest, page, probe_hash, cli_hash, backend, reference, reviewer]
    if pdf_options and any(pdf_options.values()):
        values.append(pdf_option_key_values(pdf_options))
    return hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()


def ask(input_fn=input):
    note = ''
    while True:
        answer = input_fn('[c] correct  [p] partly wrong  [w] wrong  [s] skip  [r] redisplay  [n] note  [q] quit: ').strip().lower()
        if answer in ('q','quit'): return 'quit', note
        if answer in ('r','redisplay'): return 'redisplay', note
        if answer in ('n','note'):
            note = input_fn('Note: ')[:4096]; continue
        if not answer: answer = 's'
        if answer in RATINGS:
            rating = RATINGS[answer]
            if rating in ('wrong','partial') and not note:
                note = input_fn('What looks wrong? (Enter to leave blank): ')[:4096]
            return rating, note
        print('Choose c, p, w, s, r, n, or q.')


def automatic_failure(details):
    reason = None
    if details.get('decode_status') != 'ok': reason = 'No decoded image: ' + details.get('error', '').strip()
    elif details.get('artifact_error'): reason = 'No viewable image: ' + details['artifact_error']
    elif details.get('terminal_status') in ('error', 'timeout'): reason = 'Terminal rendering failed: ' + details['terminal_status']
    if reason is None: return False
    details.update(rating='wrong', rating_source='automatic', note=reason)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', help='Files, directories, or quoted globs; mixed formats are allowed')
    parser.add_argument('--assets', action='store_true', help='Include all checked-in image assets')
    parser.add_argument('--pdfjs', action='store_true', help='Select the entire pinned tracked PDF.js corpus')
    parser.add_argument('--download', action='store_true', help='Download missing selected PDF.js files')
    parser.add_argument('--cache', type=Path, default=ROOT/'.cache/pdfjs-full')
    parser.add_argument('--format', action='append', default=[], help='Filter formats, e.g. png or pdf,png')
    parser.add_argument('--match', default='*', help='Filename/path glob')
    parser.add_argument('--range', default='all', help='1-based file selection after sorting/filtering, e.g. 1-5,8')
    parser.add_argument('--pages', default='all', help='Page/frame selection: all, first, or 1-based ranges')
    parser.add_argument('--include-negative', action='store_true')
    parser.add_argument('--list', action='store_true', help='List selected files without rendering or rating')
    parser.add_argument('--reference', action='store_true', help='Show an independent PDF reference beside actual output')
    parser.add_argument('--backend', choices=('auto','ansi','sixel'), default='auto')
    parser.add_argument('--probe', type=Path, default=ROOT/'bin/test_asset_probe')
    parser.add_argument('--cli', type=Path, default=ROOT/'bin/cupidimage')
    parser.add_argument('--report', type=Path, default=ROOT/'.cache/visual-review/ratings.jsonl')
    parser.add_argument('--resume', action='store_true', help='Skip rated items with matching content, binaries, and backend')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--memory-mib', type=int, default=1024)
    parser.add_argument('--max-pages', type=int, default=1000)
    parser.add_argument('--reviewer', default='local reviewer', help='Optional label saved with ratings')
    add_pdf_options(parser)
    args = parser.parse_args()
    pdf_options = pdf_option_values(args)
    pdf_flags = pdf_option_arguments(pdf_options)
    if not math.isfinite(args.timeout) or args.timeout <= 0 or args.memory_mib < 0 or args.max_pages < 1:
        parser.error('Invalid resource limits')
    try:
        items = collect(args.paths, args.assets, args.format, args.match, args.include_negative, args.pdfjs)
        selection = [(i+1,items[i]) for i in select_range(args.range,len(items))]
        select_range(args.pages, 1)
    except ValueError as error: parser.error(str(error))
    if not selection: parser.error('No matching assets selected')
    if args.list:
        for number,row in selection: print(f"{number}: [{row['format']}] {label(row['name'])}")
        return 0
    if not sys.stdin.isatty() or not sys.stdout.isatty(): parser.error('Visual review needs an interactive terminal; use --list for selection checks')
    if not args.probe.is_file() or not args.cli.is_file(): parser.error('Build first: make cli bin/test_asset_probe')
    if args.reference and (not shutil.which('pdfinfo') or not shutil.which('pdftoppm')):
        parser.error('--reference needs installed pdfinfo and pdftoppm')
    args.report = args.report.resolve(); args.report.parent.mkdir(parents=True,exist_ok=True)
    args.cache = args.cache.resolve()
    previous = set()
    if args.report.exists():
        existing_lines = args.report.read_text().splitlines()
        if existing_lines:
            try: first = json.loads(existing_lines[0])
            except ValueError: parser.error('Invalid existing report; choose a new --report')
            if first.get('tool') != 'cupidimage-visual-review': parser.error('Existing report is not a visual-review report; choose a different --report')
        for line in existing_lines:
            try: row = json.loads(line)
            except ValueError: parser.error('Report has an incomplete/invalid JSON line; preserve it and choose a new --report')
            if args.resume and row.get('kind') == 'rating':
                if 'review_key' not in row: parser.error('Rating lacks its fingerprint; choose a new report')
                previous.add(row['review_key'])
    counts = Counter(); stopped = False
    with tempfile.TemporaryDirectory(prefix='cupidimage-review-') as temporary, ExitStack() as stack:
        directory = Path(temporary)
        probe = directory/'probe'; cli = directory/'cli'
        shutil.copy2(args.probe.resolve(),probe); shutil.copy2(args.cli.resolve(),cli)
        probe_hash = hashlib.sha256(probe.read_bytes()).hexdigest(); cli_hash = hashlib.sha256(cli.read_bytes()).hexdigest()
        if pdf_flags and any(item['format'] == 'pdf' for _, item in selection):
            result, capability = process([str(probe), '--capabilities'], directory,
                                         args.timeout, args.memory_mib, 'capabilities')
            if result['status'] != 'ok' or not supports_pdf_options(capability.read_bytes(), pdf_options):
                parser.error('Selected probe cannot confirm the requested PDF options; rebuild bin/test_asset_probe')
        report = stack.enter_context(args.report.open('a'))
        def emit(row):
            row['timestamp'] = datetime.now(timezone.utc).isoformat()
            report.write(json.dumps(row,sort_keys=True)+'\n'); report.flush()
        emit({'kind':'run','tool':'cupidimage-visual-review','schema':1,'selected_files':len(selection),'probe_sha256':probe_hash,'cli_sha256':cli_hash,
              'backend':args.backend,'reviewer':args.reviewer,'pages':args.pages,'reference':args.reference,'pdf_options':pdf_options})
        try:
            for ordinal,(number,item) in enumerate(selection,1):
                print(f"\nFile {ordinal}/{len(selection)} (selection #{number}): {label(item['name'])}",flush=True)
                try:
                    if 'remote' in item: path,digest=fetch(item['remote'],item['revision'],args.cache,args.download)
                    else:
                        path=item['path']
                        if path.stat().st_size>64*1024*1024: raise ValueError('Input exceeds 64 MiB review limit')
                        digest=hashlib.sha256(path.read_bytes()).hexdigest()
                except Exception as error:
                    print('Input unavailable:',label(error));emit({'kind':'input-error','file':item['name'],'error':str(error)});counts['input-error']+=1;continue
                fmt='ico' if item['format']=='cur' else item['format']
                input_path=directory/('input'+path.suffix)
                shutil.copyfile(path,input_path)
                if hashlib.sha256(input_path.read_bytes()).hexdigest()!=digest:
                    emit({'kind':'input-error','file':str(path),'error':'Input changed while being copied'});counts['input-error']+=1;continue
                command=[str(probe),str(input_path),fmt]
                render_flags = pdf_flags if fmt == 'pdf' else []
                result,output=process(command+['0','count']+render_flags,directory,args.timeout,args.memory_mib,'count')
                page_count=1
                if result['status']=='ok':
                    try: page_count=int(output.read_text())
                    except ValueError: page_count=1
                reference_count=None
                if args.reference and fmt=='pdf':
                    import re
                    ref,info=process(['pdfinfo',str(input_path)],directory,args.timeout,args.memory_mib,'info')
                    if ref['status']=='ok':
                        match=re.search(rb'^Pages:\s+(\d+)',info.read_bytes(),re.MULTILINE)
                        if match: reference_count=int(match[1])
                wanted=reference_count if reference_count else page_count
                if wanted==0:
                    print('Metadata-only asset; no image to rate.');emit({'kind':'metadata-only','file':str(path)});counts['metadata-only']+=1;continue
                if wanted<0 or wanted>args.max_pages:
                    print('Page count exceeds review limit.');emit({'kind':'page-limit','file':str(path),'count':wanted});counts['page-limit']+=1;continue
                for page in select_range(args.pages,wanted):
                    key=review_key(path,digest,page,probe_hash,cli_hash,args.backend,args.reference,args.reviewer,pdf_options if fmt == 'pdf' else None)
                    if args.resume and key in previous: counts['resumed']+=1;continue
                    target=directory/'images';target.mkdir(exist_ok=True)
                    for child in target.iterdir():
                        if child.is_file():child.unlink()
                    result,raw=process(command+[str(page),'bundle']+render_flags,directory,args.timeout,args.memory_mib,'page')
                    details={'kind':'rating','file':str(path),'sha256':digest,'format':item['format'],'page':page+1,
                             'page_count':wanted,'review_key':key,'probe_sha256':probe_hash,'cli_sha256':cli_hash,
                             'backend':args.backend,'reviewer':args.reviewer,'decode_status':result['status'],'error':result['error']}
                    if fmt == 'pdf': details['pdf_options'] = pdf_options
                    actual=None
                    if result['status']=='ok':
                        try:
                            metadata,rgba,_,_,_=bundle(raw);details.update({k:v for k,v in metadata.items()if k!='count'})
                            actual=target/'actual.rgba';actual.write_bytes(struct.pack('=III',metadata['width'],metadata['height'],wanted)+rgba)
                        except Exception as error:details.update(decode_status='invalid-output',error=str(error))
                    if automatic_failure(details):
                        emit(details); counts['wrong']+=1; counts['automatic-wrong']+=1; previous.add(key)
                        print('Automatically rated WRONG:',label(details['note']),flush=True)
                        continue
                    reference=None
                    if args.reference and fmt=='pdf':
                        prefix=target/'reference'
                        ref,_=process(['pdftoppm','-f',str(page+1),'-l',str(page+1),'-singlefile','-r','96','-cropbox',str(input_path),str(prefix)],directory,args.timeout,args.memory_mib,'reference')
                        details['reference_status']=ref['status']
                        if ref['status']=='ok' and prefix.with_suffix('.ppm').exists():reference=prefix.with_suffix('.ppm')
                    try: save_visuals(target,actual,reference,read_ppm)
                    except Exception as error: details['artifact_error']=str(error)
                    if automatic_failure(details):
                        emit(details); counts['wrong']+=1; counts['automatic-wrong']+=1; previous.add(key)
                        print('Automatically rated WRONG:',label(details['note']),flush=True)
                        continue
                    preview=target/'comparison.png';caption='LEFT: cupidimage | RIGHT: independent reference'
                    if not preview.exists():preview=target/'actual-preview.png';caption='cupidimage output (no reference displayed)'
                    if not preview.exists():preview=target/'reference-preview.png';caption='REFERENCE ONLY: cupidimage failed to render'
                    if not preview.exists():caption='No image available'
                    note=''
                    while True:
                        print(f"\n{label(path.name)}: page/frame {page+1}/{wanted}\n{caption}",flush=True)
                        if details['error']:print('Decoder:',label(details['error'].strip()),flush=True)
                        if details.get('artifact_error'):print('Preview:',label(details['artifact_error']),flush=True)
                        if preview.exists():
                            flags=[] if args.backend=='auto' else ['--'+args.backend]
                            try:
                                rendered=subprocess.run([str(cli),*flags,'--fit',str(preview)],timeout=args.timeout,check=False)
                                details['terminal_status']='ok' if rendered.returncode==0 else 'error'
                            except subprocess.TimeoutExpired:
                                details['terminal_status']='timeout';print('Terminal rendering timed out.')
                        else:print('No image could be rendered.')
                        if automatic_failure(details):
                            emit(details);counts['wrong']+=1;counts['automatic-wrong']+=1;previous.add(key)
                            print('Automatically rated WRONG:',label(details['note']),flush=True);break
                        rating,new_note=ask();note=new_note or note
                        if rating=='redisplay':continue
                        if rating=='quit':stopped=True;break
                        details.update(rating=rating,rating_source="manual",note=note);emit(details);counts[rating]+=1;previous.add(key);break
                    if stopped:break
                if stopped:break
        except (KeyboardInterrupt,EOFError):stopped=True;print('\nReview stopped; completed ratings are saved.')
        emit({'kind':'summary','counts':dict(counts),'stopped':stopped})
    print('Ratings:',args.report);print(json.dumps(dict(counts),sort_keys=True))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
