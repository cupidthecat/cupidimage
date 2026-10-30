#!/usr/bin/env python3
"""Check PDF intent state, ICC tag selection and source conversion."""
import ctypes as c
import ctypes.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_icc_lut import fixture, profile, lut, lab_values
from test_pdf_icc import profile as matrix_profile
from test_pdf_type3 import stream
from test_pdf_pages import document

NAMES = ('RelativeColorimetric', 'Perceptual', 'Saturation', 'AbsoluteColorimetric')


def tags(raw, changes):
    entries = {}
    for i in range(struct.unpack_from('>I', raw, 128)[0]):
        name, offset, length = struct.unpack_from('>4sII', raw, 132 + i * 12)
        entries[name] = raw[offset:offset + length]
    for name, value in changes.items():
        if value is None:
            entries.pop(name, None)
        else:
            entries[name] = value
    header = bytearray(raw[:128])
    directory, payload = bytearray(), bytearray()
    start = 132 + len(entries) * 12
    for name, value in entries.items():
        directory += name + struct.pack('>II', start + len(payload), len(value))
        payload += value + bytes(-len(value) % 4)
    result = header + struct.pack('>I', len(entries)) + directory + payload
    result[:4] = struct.pack('>I', len(result))
    return bytes(result)


def source(inputs=3, bits=16, version=4):
    table = lambda l: lut(inputs, bits, transform=lambda x: lab_values(bits, l))
    return tags(profile(table(25), inputs, version=version, relative=table(50)),
                {b'A2B2': table(75), b'rig2': b'sig ' + bytes(4) + b'prmg'})


def main():
    probe, allocator = [str(Path(a).resolve()) for a in sys.argv[1:3]]
    checks = references = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-intents-') as tmp:
        pdf = Path(tmp) / 'case.pdf'

        def render(data, reject=False):
            nonlocal checks
            pdf.write_bytes(data)
            result = subprocess.run([probe, str(pdf), '0'], capture_output=True, timeout=30)
            assert (result.returncode != 0) == reject, (result.returncode, result.stderr)
            checks += 1
            return result.stdout

        def fault(data):
            nonlocal allocations
            pdf.write_bytes(data)
            result = subprocess.run([allocator, str(pdf), '0'], capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split('checks')[0].split()[-1])

        def paint(raw, name='', inputs=3, prefix='', suffix='', **kwargs):
            content = prefix + ('/' + name + ' ri ' if name else '')
            content += '/S cs ' + ' '.join(['.5'] * inputs) + ' scn 0 0 18 12 re f ' + suffix
            return fixture(raw, [.5] * inputs, inputs, content=content, **kwargs)

        for inputs in (1, 3, 4):
            for bits in (8, 16):
                for version in (2, 4):
                    raw = source(inputs, bits, version)
                    expected = []
                    for name in NAMES[:3]:
                        output = render(paint(raw, name, inputs))
                        expected.append(output)
                        pixel = output[12:15]
                        target = {'RelativeColorimetric':119, 'Perceptual':57 if version == 4 else 59,
                                  'Saturation':185 if version == 4 else 185}[name]
                        assert max(abs(v - target) for v in pixel) <= 2, (inputs, bits, version, name, pixel)
                    assert len(set(expected)) == 3
                    assert render(paint(raw, inputs=inputs)) == expected[0]
                    for name in ('Unknown', '', 'RelativeColorimetric'):
                        prefix = '/Perceptual ri /' + name + ' ri '
                        assert render(paint(raw, inputs=inputs, prefix=prefix)) == expected[0]
                    assert render(paint(raw, inputs=inputs, prefix='/Perceptual ri q /Saturation ri Q ')) == expected[1]
                    for intent, name in enumerate(NAMES[:3]):
                        for value, extra in [('/' + name, {}), ('8 0 R', {8:('/' + name).encode()})]:
                            assert render(paint(raw, inputs=inputs, prefix='/E gs ',
                                resources='/ExtGState << /E << /RI '+value+' >> >>', extra=extra)) == expected[intent]
                    # An absent A2B2 falls back to A2B0, not the relative table.
                    fallback = tags(raw, {b'A2B2':None})
                    assert render(paint(fallback, 'Saturation', inputs)) == expected[1]
                    # Invalid unused tables do not poison another intent's valid transform.
                    invalid = tags(raw, {b'A2B1':b'mAB '+bytes(28)})
                    render(paint(invalid, 'RelativeColorimetric', inputs), reject=True)
                    assert render(paint(invalid, 'Perceptual', inputs)) == expected[1]
                    if version == 4:
                        assert render(paint(raw, 'AbsoluteColorimetric', inputs)) == expected[0]
                    else:
                        render(paint(raw, 'AbsoluteColorimetric', inputs), reject=True)
                    fault(paint(raw, 'Perceptual', inputs))
        raw = source()
        relative = render(paint(raw, 'RelativeColorimetric'))
        perceptual = render(paint(raw, 'Perceptual'))
        saturation = render(paint(raw, 'Saturation'))
        for value in ('null', '8 0 R'):
            assert render(paint(raw, prefix='/Perceptual ri /E gs ', resources='/ExtGState << /E << /RI '+value+' >> >>', extra={8:b'null'})) == perceptual
        for bad in ('1', '(Perceptual)', '[/Perceptual]', 'true', '<<>>'):
            render(paint(raw, prefix='/E gs ', resources='/ExtGState << /E << /RI '+bad+' >> >>'), reject=True)
        for bad in ('ri', '1 ri', '(Perceptual) ri', '/Perceptual /Saturation ri'):
            render(paint(raw, prefix=bad+' '), reject=True)
        # Images have local intent overrides, including indirect/null metadata.
        for value, expected in [('/Perceptual',perceptual),('/Saturation',saturation),('/Unknown',relative),('null',relative),('8 0 R',perceptual)]:
            image=stream(bytes([128]*3),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace [/ICCBased 5 0 R] /Intent '+value)
            data=fixture(raw,[.5]*3,extra={6:image,8:b'/Perceptual'},resources='/XObject << /Im 6 0 R >>',content='q 18 0 0 12 0 0 cm /Im Do Q')
            assert render(data)==expected
            # Image execution must not leak its override back into the caller.
            assert render(fixture(raw,[.5]*3,extra={6:image,8:b'/Perceptual'},resources='/XObject << /Im 6 0 R >>',content='q 18 0 0 12 0 0 cm /Im Do Q /S cs .5 .5 .5 scn 0 0 18 12 re f'))==relative
            fault(data)
        for bad in ('1','(Perceptual)','[/Perceptual]'):
            image=stream(bytes([128]*3),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace [/ICCBased 5 0 R] /Intent '+bad)
            render(fixture(raw,[.5]*3,extra={6:image},resources='/XObject << /Im 6 0 R >>',content='q 18 0 0 12 0 0 cm /Im Do Q'),reject=True)
        # Form inheritance and local restoration, without recaching the ICC space.
        form=stream(b'/Saturation ri /S cs .5 .5 .5 scn 0 0 18 12 re f','/Type /XObject /Subtype /Form /BBox [0 0 18 12]')
        assert render(fixture(raw,[.5]*3,extra={6:form},resources='/XObject << /F 6 0 R >>',content='/Perceptual ri /F Do'))==saturation
        assert render(paint(raw,prefix='/Perceptual ri /F Do ',extra={6:form},resources='/XObject << /F 6 0 R >>'))==perceptual
        # Inline images use the same local override and restoration path.
        inline = 'q 18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /S /Intent /Perceptual /F /AHx ID 808080> EI Q'
        assert render(fixture(raw, [.5]*3, content=inline)) == perceptual
        # Stroke, text, indexed and alternate source spaces convert at painting time.
        for name, expected in [('RelativeColorimetric',relative),('Perceptual',perceptual),('Saturation',saturation)]:
            assert render(fixture(raw,[.5]*3,content='/S CS .5 .5 .5 SCN /'+name+' ri 12 w 0 6 m 18 6 l S')) == expected
            assert render(fixture(raw,[.5]*3,spaces='/I [/Indexed [/ICCBased 5 0 R] 0 <808080>]',content='/I cs 0 scn /'+name+' ri 0 0 18 12 re f')) == expected
            fn='<< /FunctionType 2 /Domain [0 1] /C0 [.5 .5 .5] /C1 [.5 .5 .5] /N 1 >>'
            assert render(fixture(raw,[.5]*3,spaces='/T [/Separation /Ink [/ICCBased 5 0 R] '+fn+']',content='/T cs .5 scn /'+name+' ri 0 0 18 12 re f')) == expected
            font=b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'
            text='/S cs .5 .5 .5 scn /'+name+' ri BT /F 9 Tf 1 2 Td (Hi) Tj ET'
            actual=render(fixture(raw,[.5]*3,resources='/Font << /F 6 0 R >>',extra={6:font},content=text))
            colors=set(tuple(actual[i:i+3]) for i in range(12,len(actual),4))
            assert tuple(expected[12:15]) in colors
        # Shading pattern parameters start in the defining content's initial state.
        function='<< /FunctionType 2 /Domain [0 1] /C0 [.5 .5 .5] /C1 [.5 .5 .5] /N 1 >>'
        shade=('<< /ShadingType 2 /ColorSpace [/ICCBased 5 0 R] /Coords [0 0 18 0] /Extend [true true] /Function '+function+' >>').encode()
        resources='/Shading << /Sh 6 0 R >> /Pattern << /P 7 0 R >>'
        pattern=b'<< /Type /Pattern /PatternType 2 /Shading 6 0 R >>'
        pattern_paint='/Pattern cs /P scn 0 0 18 12 re f'
        assert render(fixture(raw,[.5]*3,extra={6:shade,7:pattern},resources=resources,content='/Perceptual ri '+pattern_paint))==relative
        changed=b'<< /Type /Pattern /PatternType 2 /Shading 6 0 R /ExtGState << /RI /Saturation >> >>'
        assert render(fixture(raw,[.5]*3,extra={6:shade,7:changed},resources=resources,content=pattern_paint))==saturation
        for name,expected in [('Perceptual',perceptual),('Saturation',saturation)]:
            assert render(fixture(raw,[.5]*3,extra={6:shade},resources=resources,content='/'+name+' ri /Sh sh'))==expected
        form=stream(pattern_paint.encode(),'/Type /XObject /Subtype /Form /BBox [0 0 18 12]')
        repeated=fixture(raw,[.5]*3,extra={6:shade,7:pattern,8:form},resources=resources+' /XObject << /F 8 0 R >>',content='/Perceptual ri /F Do /Saturation ri /F Do')
        assert render(repeated)==saturation
        fault(repeated)
        # Tiling programs inherit initial intent; uncolored pattern bases use caller intent.
        cell=stream(b'/S cs .5 .5 .5 scn 0 0 18 12 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 18 12] /XStep 18 /YStep 12 /Resources << /ColorSpace << /S [/ICCBased 5 0 R] >> >>')
        assert render(fixture(raw,[.5]*3,extra={7:cell},resources=resources,content='/Perceptual ri '+pattern_paint))==relative
        cell=stream(b'0 0 18 12 re f','/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 18 12] /XStep 18 /YStep 12 /Resources << >>')
        assert render(fixture(raw,[.5]*3,extra={7:cell},spaces='/U [/Pattern [/ICCBased 5 0 R]]',resources=resources,content='/Perceptual ri /U cs .5 .5 .5 /P scn 0 0 18 12 re f'))==perceptual
        # Matrix/TRC profiles have no intent-specific source tables.
        for gray in (False, True):
            for version in (2, 4):
                matrix=matrix_profile(gray=gray,version=version)
                channels=1 if gray else 3
                baseline=render(paint(matrix,'RelativeColorimetric',channels))
                for name in NAMES[:3]:
                    assert render(paint(matrix,name,channels))==baseline
                if version==4:
                    assert render(paint(matrix,'AbsoluteColorimetric',channels))==baseline
        # PDF state overrides the ICC header's intent field.
        header=bytearray(raw);header[64:68]=struct.pack('>I',2)
        assert render(paint(bytes(header)))==relative
        # Metadata for an unused intent does not block a supported one.
        for invalid in (b'sig '+bytes(4), b'bad '+bytes(4)+b'prmg', b'sig '+bytes([0,0,0,1])+b'prmg'):
            malformed=tags(raw,{b'rig2':invalid})
            assert render(paint(malformed,'RelativeColorimetric'))==relative
            render(paint(malformed,'Saturation'),reject=True)
        only_saturation=tags(raw,{b'A2B0':None,b'A2B1':None})
        assert render(paint(only_saturation,'Saturation'))==saturation
        render(paint(only_saturation,'RelativeColorimetric'),reject=True)
        render(paint(only_saturation,'Perceptual'),reject=True)
        # Without a declared saturation reference gamut, no PRM black is assumed.
        unspecified=tags(raw,{b'rig2':None})
        assert abs(render(paint(unspecified,'Saturation'))[12]-185)<=1
        # Non-D50 media white changes absolute colorimetry, not the relative result.
        for xyz in ((.8,.9,.7),(.4,.5,.6)):
            white=b'XYZ '+bytes(4)+struct.pack('>3i',*[round(v*65536) for v in xyz])
            altered=tags(raw,{b'wtpt':white})
            assert render(paint(altered,'RelativeColorimetric'))==relative
            assert render(paint(altered,'AbsoluteColorimetric'))!=relative
            fault(paint(altered,'AbsoluteColorimetric'))
        for invalid in (None,b'XYZ '+bytes(4)+bytes(12),b'XYZ '+bytes(4)+bytes(8)):
            altered=tags(raw,{b'wtpt':invalid})
            assert render(paint(altered,'RelativeColorimetric'))==relative
            render(paint(altered,'AbsoluteColorimetric'),reject=True)
        if '--reference' in sys.argv:
            library=ctypes.util.find_library('lcms2');assert library
            lib=c.CDLL(library)
            lib.cmsOpenProfileFromMem.argtypes=[c.c_void_p,c.c_uint32];lib.cmsOpenProfileFromMem.restype=c.c_void_p
            lib.cmsCreate_sRGBProfile.restype=c.c_void_p
            lib.cmsCreateTransform.argtypes=[c.c_void_p,c.c_uint32,c.c_void_p,c.c_uint32,c.c_uint32,c.c_uint32];lib.cmsCreateTransform.restype=c.c_void_p
            lib.cmsDoTransform.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_uint32]
            lib.cmsDeleteTransform.argtypes=[c.c_void_p];lib.cmsCloseProfile.argtypes=[c.c_void_p]
            for inputs,space in ((1,3),(3,4),(4,6)):
                for bits in (8,16):
                    for xyz in ((.9642,1,.8249),(.8,.9,.7)):
                        white=b'XYZ '+bytes(4)+struct.pack('>3i',*[round(v*65536) for v in xyz])
                        raw=tags(source(inputs,bits),{b'wtpt':white})
                        buf=c.create_string_buffer(raw);src=lib.cmsOpenProfileFromMem(buf,len(raw));dst=lib.cmsCreate_sRGBProfile()
                        assert src and dst
                        try:
                            for mode,name in enumerate(NAMES):
                                intent=(1,0,2,3)[mode]
                                transform=lib.cmsCreateTransform(src,(1<<22)|(space<<16)|(inputs<<3),dst,(1<<22)|(4<<16)|(3<<3),intent,0x140)
                                assert transform
                                try:
                                    incoming=(c.c_double*inputs)(*([50 if inputs==4 else .5]*inputs));out=(c.c_double*3)()
                                    lib.cmsDoTransform(transform,incoming,out,1)
                                    expected=[round(max(0,min(1,v))*255) for v in out]
                                    actual=render(paint(raw,name,inputs))[12:15]
                                    assert max(abs(a-b) for a,b in zip(actual,expected))<=2,(inputs,bits,xyz,name,actual,expected)
                                    references+=1
                                finally:lib.cmsDeleteTransform(transform)
                        finally:lib.cmsCloseProfile(src);lib.cmsCloseProfile(dst)
    print(f'PDF rendering intents: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':main()
