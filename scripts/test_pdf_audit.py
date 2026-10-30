#!/usr/bin/env python3
"""Offline unit tests for corpus integrity, process limits, and comparisons."""
import contextlib
import io
import hashlib
import json
import zlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import audit_pdf_corpus
from audit_pdf_corpus import compare, fetch, inspect_output, process, safe_relative, verify_blob, read_ppm, download_only
from pdf_visuals import write_png, save_visuals, write_gallery


class AuditTests(unittest.TestCase):
    def test_paths(self):
        for name in ('../x.pdf', '/x.pdf', 'dir/../x.pdf', 'a\\b.pdf', ''):
            with self.subTest(name=name), self.assertRaises(ValueError): safe_relative(name)
        self.assertEqual(str(safe_relative('folder/a.pdf')), 'folder/a.pdf')

    def test_blob(self):
        data = b'%PDF-1.4\n%%EOF\n'
        row = {'size': len(data), 'git_blob_sha1': hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()}
        verify_blob(data, row)
        for bad in (data[:-1], b'x'+data[1:]):
            with self.assertRaises(ValueError): verify_blob(bad, row)
        with tempfile.TemporaryDirectory() as temp:
            row['path'] = 'a.pdf'; path = Path(temp) / 'revision/a.pdf'; path.parent.mkdir(); path.write_bytes(data)
            actual, digest = fetch(row, 'revision', Path(temp), False)
            self.assertEqual(actual, path); self.assertEqual(digest, hashlib.sha256(data).hexdigest())
            with self.assertRaises(FileNotFoundError): fetch({**row, 'path': 'missing.pdf'}, 'revision', Path(temp), False)

    def test_download_only(self):
        data = b'%PDF-1.4\n%%EOF\n'
        row = {'path': 'a.pdf', 'size': len(data), 'git_blob_sha1': hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()}
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp)
            output = io.StringIO()
            with patch('audit_pdf_corpus.urllib.request.urlopen', return_value=io.BytesIO(data)) as request, contextlib.redirect_stdout(output):
                self.assertEqual(download_only([row], 'revision', cache), 0)
            request.assert_called_once()
            self.assertEqual(json.loads(output.getvalue().splitlines()[-1]), {'downloaded': 1})
            self.assertEqual((cache/'revision/a.pdf').read_bytes(), data)
            output = io.StringIO()
            with patch('audit_pdf_corpus.urllib.request.urlopen', side_effect=AssertionError('cached inputs must not download')), contextlib.redirect_stdout(output):
                self.assertEqual(download_only([row], 'revision', cache), 0)
            self.assertEqual(json.loads(output.getvalue().splitlines()[-1]), {'cached': 1})

    def test_download_only_failure_preserves_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            cache=Path(temp);(cache/'revision').mkdir()
            path=cache/'revision/a.pdf';path.write_bytes(b'bad')
            row={'path':'a.pdf','size':3,'git_blob_sha1':'0'*40}
            output=io.StringIO();errors=io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                self.assertEqual(download_only([row,{**row,'path':'../escape.pdf'}],'revision',cache),1)
            self.assertEqual(path.read_bytes(),b'bad')
            self.assertEqual(json.loads(output.getvalue().splitlines()[-1]),{'input-error':2})
            self.assertIn('Input unavailable:',errors.getvalue())

    def test_download_only_cli_needs_no_renderer(self):
        data=b'%PDF-1.4\n%%EOF\n'
        row={'path':'a.pdf','size':len(data),'git_blob_sha1':hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()}
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);fixtures=root/'tests/fixtures';fixtures.mkdir(parents=True)
            (fixtures/'pdfjs-corpus.json').write_text(json.dumps({'revision':'revision','files':[row]}))
            cache=root/'cache';(cache/'revision').mkdir(parents=True);(cache/'revision/a.pdf').write_bytes(data)
            report=root/'ratings.jsonl';report.write_text('preserve ratings')
            args=['audit','--download-only','--cache',str(cache),'--probe',str(root/'absent'),'--report',str(report)]
            with patch.object(audit_pdf_corpus,'ROOT',root), patch.object(sys,'argv',args), contextlib.redirect_stdout(io.StringIO()), patch('audit_pdf_corpus.process',side_effect=AssertionError('must not render')):
                self.assertEqual(audit_pdf_corpus.main(),0)
            self.assertEqual(report.read_text(),'preserve ratings')
            for extra in ('--reference','--resume','--keep-images','--pdf-text-aa','--pdf-tt-hinting','--pdf-cff-hinting','--pdf-type1-hinting'):
                with patch.object(sys,'argv',args+[extra]), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                    audit_pdf_corpus.main()
                self.assertEqual(error.exception.code,2)

    def test_pixels(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'raw'; ppm = Path(temp) / 'reference.ppm'
            path.write_bytes(struct.pack('=III', 1, 1, 1) + b'\x10\x20\x30\xff')
            details = inspect_output(path)
            self.assertEqual((details['width'], details['height']), (1, 1))
            ppm.write_bytes(b'P6\n# comment\n1 1\n255\n\x10\x20\x30')
            self.assertEqual(compare(ppm, path, (1, 1))['status'], 'exact')
            self.assertEqual(compare(ppm, path, (2, 1))['status'], 'dimension-mismatch')
            ppm.write_bytes(b'P6\n1 1\n255\n\x11\x20\x30')
            self.assertEqual(compare(ppm, path, (1, 1))['max_channel_difference'], 1)
            path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaises(ValueError): inspect_output(path)

    def test_visual_artifacts(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); raw = directory/'actual.rgba'; ppm = directory/'reference.ppm'
            raw.write_bytes(struct.pack('=III', 1, 1, 1)+bytes([255, 0, 0, 255]))
            ppm.write_bytes(b'P6\n1 1\n255\n'+bytes([255, 0, 0]))
            save_visuals(directory, raw, ppm, read_ppm)
            for name in ('actual.png','reference.png','comparison.png','difference.png'):
                data=(directory/name).read_bytes();self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
                pos=8;compressed=bytearray()
                while pos<len(data):
                    n,=struct.unpack_from('>I',data,pos);kind=data[pos+4:pos+8];chunk=data[pos+8:pos+8+n]
                    self.assertEqual(zlib.crc32(kind+chunk),struct.unpack_from('>I',data,pos+8+n)[0])
                    if kind==b'IDAT':compressed.extend(chunk)
                    pos+=12+n
                decoded=zlib.decompress(compressed)
                if name=='actual.png':self.assertEqual(decoded,b'\0'+bytes([255,0,0,255]))
                if name=='difference.png':self.assertEqual(decoded,bytes(4))
            report=directory/'report.jsonl'
            report.write_text(json.dumps({'kind':'page','file':'<script>alert(1)</script>.pdf','page':1,'status':'ok','artifacts':str(directory)})+'\n')
            gallery=write_gallery(report).read_text()
            self.assertIn('&lt;script&gt;',gallery);self.assertNotIn('<script>',gallery)
            self.assertIn('comparison.png',gallery)

    def test_process(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            result, output = process([sys.executable, '-c', 'print("ok")'], directory, 5, 0, 'ok')
            self.assertEqual(result['status'], 'ok'); self.assertEqual(output.read_text().strip(), 'ok')
            result, _ = process([sys.executable, '-c', 'raise SystemExit(3)'], directory, 5, 0, 'error')
            self.assertEqual(result['status'], 'error')
            result, _ = process([sys.executable, '-c', 'import time; time.sleep(10)'], directory, .1, 0, 'timeout')
            self.assertEqual(result['status'], 'timeout')


if __name__ == '__main__':
    unittest.main()
