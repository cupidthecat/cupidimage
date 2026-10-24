#!/usr/bin/env python3
"""Offline unit tests for corpus integrity, process limits, and comparisons."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from audit_pdf_corpus import compare, fetch, inspect_output, process, safe_relative, verify_blob


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
