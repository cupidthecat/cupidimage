#!/usr/bin/env python3
"""RFC 1950/1951 byte-level tests, with Python zlib as an independent oracle."""
import pathlib
import random
import struct
import subprocess
import sys
import tempfile
import zlib

class Bits:
    def __init__(self):
        self.bits = []
    def put(self, value, count):
        self.bits.extend((value >> i) & 1 for i in range(count))
    def bytes(self):
        result = bytearray((len(self.bits) + 7) // 8)
        for i, bit in enumerate(self.bits):
            result[i // 8] |= bit << (i % 8)
        return bytes(result)

def wrap(data, raw, cmf=0x78):
    flg = (-(cmf << 8)) % 31
    return bytes([cmf, flg]) + data + struct.pack('>I', zlib.adler32(raw))

def literal_block(raw, distance_count=1, only_end=False):
    # Code-length alphabet: 0 and 1, each one bit. The literal alphabet has
    # symbols 0 and EOB, or only EOB. No distances are used (RFC 1951 3.2.7).
    bits = Bits()
    bits.put(5, 3)  # final, dynamic
    bits.put(0, 5)
    bits.put(distance_count - 1, 5)
    bits.put(14, 4)
    order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1]
    for symbol in order:
        bits.put(int(symbol in (0, 1)), 3)
    for symbol in range(257):
        bits.put(int(symbol == 256 or symbol == 0 and not only_end), 1)
    for _ in range(distance_count):
        bits.put(0, 1)
    for value in raw:
        assert value == 0
        bits.put(0, 1)
    bits.put(0 if only_end else 1, 1)
    return wrap(bits.bytes(), raw)

binary = str(pathlib.Path(sys.argv[1]).resolve())
checks = 0
with tempfile.TemporaryDirectory(prefix='cupidimage-inflate-') as directory:
    path = pathlib.Path(directory) / 'stream.zlib'
    def check(label, encoded, expected, capacity=None, oracle=True):
        global checks
        checks += 1
        if oracle:
            try:
                decoded = zlib.decompress(encoded)
            except zlib.error:
                assert expected is None, label
            else:
                if expected is not None:
                    assert decoded == expected, label
        path.write_bytes(encoded)
        cap = capacity if capacity is not None else (len(expected) if expected is not None else 65536)
        result = subprocess.run([binary, str(path), str(cap)], capture_output=True, timeout=5)
        assert result.returncode == (1 if expected is None else 0), (label, result.returncode, result.stderr)
        assert result.stdout == (b'' if expected is None else expected), label

    rng = random.Random(1951)
    sources = [b'', b'\0', b'hello', bytes(range(256)), b'abcde' * 2000,
               rng.randbytes(10000), b'\0' * 40000, bytes(range(256)) * 256]
    for raw in sources:
        for level in (0, 1, 6, 9):
            for strategy in (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FIXED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE):
                coder = zlib.compressobj(level, zlib.DEFLATED, 15, 8, strategy)
                encoded = coder.compress(raw) + coder.flush()
                check(('oracle', len(raw), level, strategy), encoded, raw)
                if raw:
                    check('short output buffer', encoded, None, len(raw) - 1)
    for raw, only_end in [(b'', True), (b'\0', False), (b'\0' * 100, False)]:
        check('literal-only dynamic alphabet', literal_block(raw, only_end=only_end), raw)
    # RFC permits HDIST to describe 32 codes, although codes 30/31 must never
    # occur in data. Some zlib versions reject even unused trailing entries.
    check('32 unused distance lengths', literal_block(b'\0', 32), b'\0', oracle=False)
    good = zlib.compress(b'bounded checksum and truncation fixture' * 4)
    for end in range(1, len(good)):
        check(('truncated', end), good[:end], None)
    for i in range(4):
        bad = bytearray(good)
        bad[-1 - i] ^= 1
        check(('bad Adler byte', i), bad, None)
    check('bad FCHECK', bytes([good[0], good[1] ^ 1]) + good[2:], None)
    check('unsupported window', wrap(good[2:-4], b'bounded checksum and truncation fixture' * 4, 0x88), None)
    check('trailing data', good + b'junk', None, oracle=False)
    check('raw stream is not zlib', good[2:-4], None)
    check('reserved block type', wrap(b'\x07', b''), None)
    check('stored length mismatch', wrap(b'\x01\x01\x00\xff\xffx', b'x'), None)
    check('empty fixed block at exact byte end', wrap(b'\x03\x00', b''), b'')
    for clen in [(1, 1, 1, 0), (2, 2, 0, 0)]:
        bits = Bits(); bits.put(5, 3); bits.put(0, 5); bits.put(0, 5); bits.put(0, 4)
        for length in clen:
            bits.put(length, 3)
        bits.put(0, 24)
        check('invalid code-length alphabet', wrap(bits.bytes(), b''), None)
    # A complete code-length alphabet with symbol 18 repeats zero 138 times,
    # twice: 276 lengths exceed the requested 258, rather than being clipped.
    bits = Bits(); bits.put(5, 3); bits.put(0, 5); bits.put(0, 5); bits.put(0, 4)
    for length in [0, 0, 1, 1]: bits.put(length, 3)
    for _ in range(2): bits.put(1, 1); bits.put(127, 7)
    check('code-length repeat overrun', wrap(bits.bytes(), b''), None)
print(f'DEFLATE conformance: {checks} checks passed')

# Prefix decoding reports the checksum boundary, while the strict entry point
# continues rejecting trailing content. Lookahead must not consume PDF tokens.
with tempfile.TemporaryDirectory(prefix='cupidimage-inflate-prefix-') as directory:
    path=pathlib.Path(directory)/'prefix.zlib'
    for level in (0,1,6,9):
        for raw in (b'', b' EI Q endstream ', bytes(range(256))*32):
            encoded=zlib.compress(raw,level)
            for tail in (b'',b' EI q ',b'\x00\xff\x00'):
                path.write_bytes(encoded+tail)
                result=subprocess.run([binary,str(path),str(len(raw)), 'prefix'],capture_output=True)
                assert result.returncode==0 and result.stdout==raw and int(result.stderr)==len(encoded)
                checks+=1
            for bad in (encoded[:-1],encoded[:-1]+bytes([encoded[-1]^1])):
                path.write_bytes(bad+b' EI ')
                result=subprocess.run([binary,str(path),str(len(raw)), 'prefix'],capture_output=True)
                assert result.returncode==1,(level,len(raw),result.returncode)
                checks+=1
print(f'DEFLATE including prefix boundaries: {checks} checks passed')
