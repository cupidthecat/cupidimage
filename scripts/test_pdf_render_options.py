#!/usr/bin/env python3
"""Exercise PDF probe options and report bookkeeping with owned one-page fixtures."""
import contextlib
import hashlib
import io
import itertools
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch
import review_assets
from pdf_render_options import OPTIONS, pdf_option_arguments, supports_pdf_options


class Terminal(io.StringIO):
    def isatty(self):
        return True


def main():
    pages, assets, cli = [Path(p).resolve() for p in sys.argv[1:4]]
    checks = 0
    run = subprocess.run
    for probe in (pages, assets):
        capabilities = subprocess.check_output([str(probe), '--capabilities'])
        assert supports_pdf_options(capabilities, {field: True for _, field, _ in OPTIONS})
        checks += 1
    fixtures = [Path('assets/pdf/native-truetype-hinting.pdf').resolve(),
                Path('assets/pdf/native-cff-hinting.pdf').resolve(),
                Path('assets/pdf/native-type1-hinting.pdf').resolve()]
    images = {}
    for fixture in fixtures:
        for bits in itertools.product((False, True), repeat=len(OPTIONS)):
            values = {field: bits[i] for i, (_, field, _) in enumerate(OPTIONS)}
            flags = pdf_option_arguments(values)
            page = subprocess.check_output([str(pages), str(fixture), '0', *flags])
            positional = subprocess.check_output([str(pages), str(fixture), '0', *[str(int(v)) for v in bits]])
            assert page == positional
            asset = subprocess.check_output([str(assets), str(fixture), 'pdf', '0', 'pixels', *flags])
            assert page[:12] == asset[:12] and page[12:] == asset[28:]
            w, h, n = struct.unpack('=3I', page[:12])
            assert len(page) == 12 + w*h*4 and n == 1
            assert subprocess.check_output([str(assets), str(fixture), 'pdf', '0', 'count', *flags]) == b'1\n'
            images[fixture.name, bits] = page[12:]
            checks += 1
    assert images[fixtures[0].name, (True, True, False, False)] != images[fixtures[0].name, (True, False, False, False)]
    assert images[fixtures[1].name, (True, False, True, False)] != images[fixtures[1].name, (True, False, False, False)]
    assert images[fixtures[2].name, (True, False, False, True)] != images[fixtures[2].name, (True, False, False, False)]
    for fixture in fixtures[:2]:
        for bits in itertools.product((False, True), repeat=3):
            assert images[fixture.name, (*bits, False)] == images[fixture.name, (*bits, True)]
    for command in ([str(pages), str(fixtures[0]), '0', '--unknown'],
                    [str(assets), str(fixtures[0]), 'pdf', '0', 'pixels', '--unknown'],
                    [str(assets), 'assets/png/basic.png', 'png', '0', 'pixels', '--pdf-text-aa']):
        result = run(command, capture_output=True)
        assert result.returncode == 2 and not result.stdout
        checks += 1
    profile = {'text_antialias': True, 'truetype_hinting': False, 'cff_hinting': True}
    flags = pdf_option_arguments(profile)
    expected = images[fixtures[1].name, (True, False, True, False)]
    with tempfile.TemporaryDirectory(prefix='cupid-pdf-option-tests-') as temporary:
        root = Path(temporary)
        corpus = root / 'corpus'
        corpus.mkdir()
        shutil.copyfile(fixtures[1], corpus / 'case.pdf')
        report = root / 'audit.jsonl'
        command = [sys.executable, 'scripts/audit_pdf_corpus.py', '--corpus', str(corpus),
                   '--probe', str(pages), '--memory-mib', '0', '--report', str(report), *flags]
        result = run(command, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (result.stdout, result.stderr)
        records = [json.loads(line) for line in report.read_text().splitlines()]
        assert records[0]['configuration']['pdf_options'] == profile
        page_records = [row for row in records if row['kind'] == 'page']
        assert len(page_records) == 1 and page_records[0]['rgba_sha256'] == hashlib.sha256(expected).hexdigest()
        result = run(command + ['--resume'], capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        records = [json.loads(line) for line in report.read_text().splitlines()]
        assert len([row for row in records if row['kind'] == 'page']) == 1
        saved = report.read_bytes()
        result = run(command + ['--resume', '--pdf-tt-hinting'], capture_output=True, text=True, timeout=30)
        assert result.returncode == 2 and report.read_bytes() == saved
        checks += 3
        type1_report=root/'type1-audit.jsonl'
        type1_corpus=root/'type1-corpus'
        type1_corpus.mkdir()
        shutil.copyfile(fixtures[2],type1_corpus/'case.pdf')
        type1_command=[sys.executable,'scripts/audit_pdf_corpus.py','--corpus',str(type1_corpus),
                       '--probe',str(pages),'--memory-mib','0','--report',str(type1_report),
                       '--pdf-text-aa','--pdf-type1-hinting']
        result=run(type1_command,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,(result.stdout,result.stderr)
        type1_records=[json.loads(line) for line in type1_report.read_text().splitlines()]
        assert type1_records[0]['configuration']['pdf_options']['type1_hinting'] is True
        assert next(row for row in type1_records if row['kind']=='page')['rgba_sha256']==hashlib.sha256(images[fixtures[2].name,(True,False,False,True)]).hexdigest()
        checks+=1
        # An old probe must fail before it can overwrite a report or rate inputs.
        old = root / 'old-probe'
        old.write_text('#!/usr/bin/env python3\nimport sys\nsys.exit(2)\n')
        old.chmod(0o755)
        unsupported = command.copy()
        unsupported[unsupported.index('--probe') + 1] = str(old)
        result = run(unsupported, capture_output=True, text=True, timeout=30)
        assert result.returncode == 2 and 'requested PDF options' in result.stderr
        assert report.read_bytes() == saved
        checks += 1
        visual = root / 'visual.jsonl'
        arguments = ['review_assets.py', str(fixtures[1]), '--probe', str(assets), '--cli', str(cli),
                     '--backend', 'sixel', '--memory-mib', '0', '--report', str(visual), *flags]
        displayed = []

        def display(command, *args, **kwargs):
            if Path(command[0]).name == 'cli':
                assert '--sixel' in command and not any(flag in command for flag, _, _ in OPTIONS)
                raw = (Path(command[-1]).parent / 'actual.rgba').read_bytes()
                displayed.append(raw[12:])
                return subprocess.CompletedProcess(command, 0)
            return run(command, *args, **kwargs)

        def review(argv, answer=('skip', 'automated harness fixture')):
            with patch.object(sys, 'argv', argv), patch.object(sys, 'stdin', Terminal()), \
                    patch.object(sys, 'stdout', Terminal()), contextlib.redirect_stderr(io.StringIO()), \
                    patch('review_assets.subprocess.run', side_effect=display), \
                    patch('review_assets.ask', return_value=answer) as ask:
                result = review_assets.main()
                return result, ask.call_count

        assert review(arguments) == (0, 1)
        assert displayed == [expected]
        records = [json.loads(line) for line in visual.read_text().splitlines()]
        ratings = [row for row in records if row['kind'] == 'rating']
        assert len(ratings) == 1 and ratings[0]['pdf_options'] == profile
        assert records[0]['pdf_options'] == profile
        assert review(arguments + ['--resume']) == (0, 0)
        assert len(displayed) == 1
        alternate = [value for value in arguments if value != '--pdf-cff-hinting'] + ['--resume']
        assert review(alternate) == (0, 1)
        assert displayed[-1] == images[fixtures[1].name, (True, False, False, False)]
        records = [json.loads(line) for line in visual.read_text().splitlines()]
        ratings = [row for row in records if row['kind'] == 'rating']
        assert len(ratings) == 2 and ratings[0]['review_key'] != ratings[1]['review_key']
        png = Path('assets/png/basic.png').resolve()
        png_pixels = subprocess.check_output([str(assets), str(png), 'png', '0', 'pixels'])[28:]
        mixed_report = root / 'mixed.jsonl'
        mixed = arguments.copy()
        mixed.insert(2, str(png))
        mixed[mixed.index('--report') + 1] = str(mixed_report)
        before = len(displayed)
        assert review(mixed) == (0, 2)
        assert expected in displayed[before:] and png_pixels in displayed[before:]
        mixed_rows = [json.loads(line) for line in mixed_report.read_text().splitlines()]
        mixed_ratings = {row['format']: row for row in mixed_rows if row['kind'] == 'rating'}
        assert mixed_ratings['pdf']['pdf_options'] == profile and 'pdf_options' not in mixed_ratings['png']
        png_row = mixed_ratings['png']
        assert png_row['review_key'] == review_assets.review_key(
            png_row['file'], png_row['sha256'], 0, png_row['probe_sha256'], png_row['cli_sha256'],
            'sixel', False, png_row['reviewer'])
        checks += 1
        saved = visual.read_bytes()
        unsupported = arguments.copy()
        unsupported[unsupported.index('--probe') + 1] = str(old)
        try:
            review(unsupported)
            raise AssertionError('Old probe was accepted for PDF rendering options')
        except SystemExit as error:
            assert error.code == 2
        assert visual.read_bytes() == saved
        checks += 4
    print(f'PDF rendering-option tools: {checks} probe, audit and simulated-review checks passed; no corpus ratings run')


if __name__ == '__main__':
    main()
