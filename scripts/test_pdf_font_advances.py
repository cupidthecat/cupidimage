#!/usr/bin/env python3
"""Check fallback font advances, cached widths and allocation-error propagation."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_cff_render import fixture as cff_pdf, BOX as CFF_BOX
from test_pdf_cff import fixture as cff_font
from test_pdf_type2 import number, command as cff_command
from test_pdf_type1_render import fixture as type1_pdf
from test_pdf_type1_charstrings import command, BOX as TYPE1_BOX, WIDTH
from test_pdf_truetype_bearings import bearing_font
from test_pdf_truetype import tables, replace_tables
from test_pdf_embedded_fonts import fixture as tt_pdf
try:
    import freetype
except ImportError:
    freetype = None


def main():
    probe,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks=allocations=font_references=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-font-advances-') as directory:
        path=Path(directory)/'case.pdf'
        def render(pdf,reject=False):
            nonlocal checks
            path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert run.returncode==(3 if reject else 0),run.stderr
            checks+=1
            return run.stdout
        def check_tt_metrics(font, glyph, expected):
            nonlocal font_references
            if freetype is None:
                return
            font_path = path.with_name('metrics.ttf')
            font_path.write_bytes(font)
            face = freetype.Face(str(font_path))
            face.load_glyph(glyph, freetype.FT_LOAD_NO_SCALE | freetype.FT_LOAD_NO_HINTING |
                            freetype.FT_LOAD_NO_BITMAP)
            assert face.glyph.advance.x == expected, (glyph, face.glyph.advance.x, expected)
            font_references += 1
        def fault(pdf,reject=False):
            nonlocal allocations
            path.write_bytes(pdf)
            run=subprocess.run([allocator,str(path),'1' if reject else '0'],capture_output=True,text=True,timeout=40)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
        for kind in ('cff','type1'):
            for width in (-200,0,600,1000):
                if kind=='cff':
                    font=cff_font(programs=[b'\x0e',b'\x0e',number(width-100)+CFF_BOX,b'\x0e'])
                    make=lambda content,metrics: cff_pdf(content,font=font,metrics=metrics)
                else:
                    glyphs=[('.notdef',WIDTH+b'\x0e'),('A',command([0,width],13)+TYPE1_BOX[len(WIDTH):])]
                    make=lambda content,metrics: type1_pdf(content,glyphs=glyphs,metrics=metrics)
                explicit=f'/FirstChar 65 /LastChar 65 /Widths [{width}]'
                for mode in range(8):
                    content=f'BT /F 18 Tf 48 36 Td {mode} Tr (AAA) Tj 0 Tr (A) Tj ET'
                    if mode>=4:content+=' 1 0 0 rg 0 0 120 90 re f'
                    assert render(make(content,''))==render(make(content,explicit)),(kind,width,mode)
                content='BT /F 30 Tf 20 30 Td (AAA) Tj ET'
                fault(make(content,''))
            malformed=b'\x02'
            if kind=='cff':
                bad=lambda content: cff_pdf(content,font=cff_font(programs=[b'\x0e',b'\x0e',malformed,b'\x0e']),metrics='')
            else:
                bad=lambda content: type1_pdf(content,glyphs=[('.notdef',WIDTH+b'\x0e'),('A',malformed)],metrics='')
            pdf=bad('BT /F 30 Tf 3 Tr (A) Tj ET')
            render(pdf,True);fault(pdf,True)
        # The Type 1 width is a vector transformed by FontMatrix, not just wx.
        glyphs=[('.notdef',WIDTH+b'\x0e'),('A',command([0,0,600,200],[12,7])+TYPE1_BOX[len(WIDTH):])]
        content='BT /F 30 Tf 20 30 Td (AAA) Tj ET'
        kwargs={'glyphs':glyphs,'matrix':'[.001 0 .0005 .001 0 0]'}
        assert render(type1_pdf(content,metrics='',**kwargs))==render(type1_pdf(content,metrics='/FirstChar 65 /LastChar 65 /Widths [700]',**kwargs))
        fault(type1_pdf(content,metrics='',**kwargs))
        accent=cff_command([0,0],21)+cff_command([100,0,0,100,-100,0],5)+b'\x0e'
        composite=cff_command([900,100,600,65,194],14)
        font=cff_font(glyphs=5,charset=b'\0\0\1\0\x22\0\x7d\0\xab',strings=(),programs=[b'\x0e',b'\x0e',CFF_BOX,accent,composite])
        content='BT /F 30 Tf 20 30 Td <C1C1C1> Tj ET'
        assert render(cff_pdf(content,font=font,metrics=''))==render(cff_pdf(content,font=font,metrics='/FirstChar 193 /LastChar 193 /Widths [1000]'))
        fault(cff_pdf(content,font=font,metrics=''))
        composite=command([0,1000],13)+command([0,100,600,65,194],(12,6))
        accent=WIDTH+command([0,0],21)+command([100,0],5)+command([0,100],5)+command([-100,0],5)+b'\x09\x0e'
        glyphs=[('.notdef',WIDTH+b'\x0e'),('A',TYPE1_BOX),('acute',accent),('Aacute',composite)]
        assert render(type1_pdf(content,glyphs=glyphs,metrics=''))==render(type1_pdf(content,glyphs=glyphs,metrics='/FirstChar 193 /LastChar 193 /Widths [1000]'))
        fault(type1_pdf(content,glyphs=glyphs,metrics=''))
        for inherited in (0, 512, 1000, 65535):
            for component in (0, 1):
                font = bearing_font({}, use_metrics=component)
                directory = tables(font)
                _, offset, length = directory[b'hmtx']
                metrics = bytearray(font[offset:offset + length])
                struct.pack_into('>H', metrics, 4, inherited)
                struct.pack_into('>H', metrics, 16, 700)
                font = replace_tables(font, {b'hmtx': metrics})
                check_tt_metrics(font, 4, inherited)
                for mode in range(8):
                    content = f'BT /F 18 Tf 5 30 Td {mode} Tr (DDD) Tj 0 Tr (D) Tj ET'
                    if mode >= 4:
                        content += ' 1 0 0 rg 0 0 120 90 re f'
                    implicit = tt_pdf(content, font_data=font, metrics='')
                    explicit = tt_pdf(content, font_data=font,
                                      metrics=f'/FirstChar 68 /LastChar 68 /Widths [{inherited}]')
                    assert render(implicit) == render(explicit), ('TrueType inherited advance', inherited, component, mode)
                fault(tt_pdf('BT /F 18 Tf 5 30 Td (DDD) Tj ET', font_data=font, metrics=''))
        # Nested inheritance and the final selected component use the same
        # bounded outline walk as drawing, including cached advances.
        for variant, expected_width, code in (('nested', 512, 'E'), ('last', 901, 'D')):
            font = bearing_font({}, use_metrics=0)
            directory = tables(font)
            def table(tag):
                _, offset, length = directory[tag]
                return bytearray(font[offset:offset + length])
            metrics, glyf, loca = table(b'hmtx'), table(b'glyf'), table(b'loca')
            struct.pack_into('>H', metrics, 4, 512)
            struct.pack_into('>H', metrics, 8, 901)
            struct.pack_into('>H', metrics, 16, 700)
            struct.pack_into('>H', metrics, 20, 900)
            if variant == 'nested':
                start = struct.unpack_from('>H', loca, 5 * 2)[0] * 2
                flags = struct.unpack_from('>H', glyf, start + 10)[0]
                struct.pack_into('>HH', glyf, start + 10, flags | 512, 4)
                struct.pack_into('>h', glyf, start + 6, 850)
            else:
                start = struct.unpack_from('>H', loca, 4 * 2)[0] * 2
                at = start + 10
                flags = struct.unpack_from('>H', glyf, at)[0]
                at += 4 + (4 if flags & 1 else 2) + (2 if flags & 8 else 4 if flags & 64 else 8 if flags & 128 else 0)
                flags = struct.unpack_from('>H', glyf, at)[0]
                struct.pack_into('>HH', glyf, at, flags | 512, 2)
                struct.pack_into('>h', glyf, start + 6, 950)
            font = replace_tables(font, {b'hmtx': metrics, b'glyf': glyf})
            check_tt_metrics(font, 5 if variant == 'nested' else 4, expected_width)
            content = f'BT /F 18 Tf 5 30 Td ({code * 3}) Tj ET'
            explicit = f'/FirstChar {ord(code)} /LastChar {ord(code)} /Widths [{expected_width}]'
            assert render(tt_pdf(content, font_data=font, metrics='')) == render(tt_pdf(content, font_data=font, metrics=explicit))
            fault(tt_pdf(content, font_data=font, metrics=''))
        descriptor = b'<< /Type /FontDescriptor /FontName /CupidImageTestShapes /Flags 32 /FontBBox [0 0 1000 1400] /ItalicAngle 0 /Ascent 1000 /Descent -200 /CapHeight 600 /StemV 80 /FontFile2 9 0 R /MissingWidth 321 >>'
        content = 'BT /F 18 Tf 5 30 Td (DDD) Tj ET'
        assert render(tt_pdf(content, font_data=font, metrics='', extra={8: descriptor})) == render(tt_pdf(content, font_data=font, metrics='/FirstChar 68 /LastChar 68 /Widths [321]'))
        assert render(tt_pdf(content, font_data=font, metrics='/FirstChar 68 /LastChar 68 /Widths [700]')) != render(tt_pdf(content, font_data=font, metrics=''))
        directory = tables(font)
        _, at, length = directory[b'glyf']; glyf = bytearray(font[at:at + length])
        _, at, _ = directory[b'loca']; start = struct.unpack_from('>H', font, at + 4 * 2)[0] * 2
        struct.pack_into('>H', glyf, start + 12, 4)
        broken = replace_tables(font, {b'glyf': glyf})
        invisible = 'BT /F 18 Tf 3 Tr (D) Tj ET'
        render(tt_pdf(invisible, font_data=broken, metrics=''), True)
        fault(tt_pdf(invisible, font_data=broken, metrics=''), True)
        render(tt_pdf(invisible, font_data=broken, metrics='/FirstChar 68 /LastChar 68 /Widths [700]'))
        # An invisible glyph without inherited metrics needs only its own hmtx
        # entry, even when its unused outline is cyclic.
        at = start + 10
        while True:
            flags = struct.unpack_from('>H', glyf, at)[0]
            struct.pack_into('>H', glyf, at, flags & ~512)
            at += 4 + (4 if flags & 1 else 2) + (2 if flags & 8 else 4 if flags & 64 else 8 if flags & 128 else 0)
            if not flags & 32:
                break
        own_metrics = replace_tables(font, {b'glyf': glyf})
        render(tt_pdf(invisible, font_data=own_metrics, metrics=''))
        for count,reject in ((65536,False),(65537,True)):
            render(tt_pdf('BT /F 1 Tf 3 Tr (' + 'D' * count + ') Tj ET', font_data=font, metrics=''), reject)
        for count,reject in ((65536,False),(65537,True)):
            render(cff_pdf('BT /F 1 Tf 3 Tr ('+'A'*count+') Tj ET',metrics=''),reject)
    print(f'PDF font advance checks: {checks}; {allocations} allocation/cache comparisons; '
          f'{font_references} independent unhinted font metrics (not damaged-PDF parity)')


if __name__=='__main__':
    main()
