#!/usr/bin/env python3
"""Check descriptor-based Latin substitutes without overriding PDF layout."""
from pathlib import Path
import subprocess
import sys
import tempfile
from generate_pdf_extended_fonts import FONTS, SOURCE
from test_pdf_substitution import fixture as base_fixture, ALIASES
from test_pdf_embedded_fonts import FONT
from test_pdf_type3 import stream

FACES=(('Helvetica','Helvetica-Bold','Helvetica-Oblique','Helvetica-BoldOblique'),
       ('Times-Roman','Times-Bold','Times-Italic','Times-BoldItalic'),
       ('Courier','Courier-Bold','Courier-Oblique','Courier-BoldOblique'))


def fixture(name='CupidUnknown',angle='0',**kwargs):
    extra=dict(kwargs.pop('extra',{}) or {})
    flags=kwargs.get('flags',32)
    descriptor=kwargs.get('descriptor','')
    embedded=' /FontFile2 7 0 R' if kwargs.get('embedded') is not None else ''
    angle_entry='' if angle is None else '/ItalicAngle '+angle
    extra[6]=(f'<< /Type /FontDescriptor /FontName /{name} /Flags {flags} /FontBBox [-2000 -2000 3000 3000] '
              f'{angle_entry} /Ascent 2000 /Descent -1000 /CapHeight 1000 /StemV 80 {descriptor}{embedded} >>')
    return base_fixture(name=name,extra=extra,**kwargs)


def main():
    probe,allocator=[str(Path(a).resolve()) for a in sys.argv[1:3]]
    checks=allocations=0
    fonts={name:(SOURCE/file).read_bytes() for name,file in FONTS.items()}
    with tempfile.TemporaryDirectory(prefix='cupidimage-font-descriptors-') as tmp:
        pdf=Path(tmp)/'case.pdf'
        def render(data,reject=False):
            nonlocal checks
            pdf.write_bytes(data);result=subprocess.run([probe,str(pdf),'0'],capture_output=True,timeout=30)
            assert (result.returncode!=0)==reject,(result.returncode,result.stderr)
            checks+=1
            return result.stdout
        def fault(data):
            nonlocal allocations
            pdf.write_bytes(data);result=subprocess.run([allocator,str(pdf),'0'],capture_output=True,text=True,timeout=120)
            assert result.returncode==0,(result.stdout,result.stderr)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for subtype in ('Type1','TrueType'):
            for family,bits in enumerate((0,2,1)):
                for weight in range(100,1000,100):
                    for italic in (False,True):
                        flags=32|bits|(64 if italic else 0)
                        face=FACES[family][(1 if weight>=600 else 0)+(2 if italic else 0)]
                        data=fixture(subtype=subtype,flags=flags,descriptor='/FontWeight '+str(weight))
                        assert render(data)==render(fixture(subtype=subtype,flags=flags,embedded=fonts[face]))
                fault(data)
            for angle in ('-12','12','-.01'):
                assert render(fixture(subtype=subtype,angle=angle))==render(fixture(subtype=subtype,embedded=fonts['Helvetica-Oblique']))
            # ForceBold is not a declaration that the font's weight is bold.
            assert render(fixture(subtype=subtype,flags=32|262144))==render(fixture(subtype=subtype,embedded=fonts['Helvetica']))
            # FixedPitch wins over Serif when no explicit family is recognized.
            assert render(fixture(subtype=subtype,flags=35))==render(fixture(subtype=subtype,embedded=fonts['Courier']))
        families=(('Arial',0),('Helvetica',0),('Liberation Sans',0),('LiberationSans',0),
                  ('Times New Roman',1),('Times',1),('Liberation Serif',1),('LiberationSerif',1),
                  ('Courier New',2),('Courier',2),('Liberation Mono',2),('LiberationMono',2))
        for name,family in families:
            for representation,extra in [('('+name+')',{}),('<'+name.encode().hex()+'>',{}),('8 0 R',{8:('('+name+')').encode()})]:
                data=fixture(flags=35,descriptor='/FontFamily '+representation+' /FontWeight 700',extra=extra,angle='-10')
                assert render(data)==render(fixture(flags=35,embedded=fonts[FACES[family][3]]))
            fault(data)
        assert render(fixture(descriptor=r'/FontFamily (Times\040New Roman)'))==render(fixture(embedded=fonts['Times-Roman']))
        for family in ('(Unknown Family)','()',r'(Arial\000Unknown)','null','99 0 R'):
            assert render(fixture(flags=34,descriptor='/FontFamily '+family))==render(fixture(flags=34,embedded=fonts['Times-Roman']))
        for key in ('FontWeight','FontFamily','ItalicAngle'):
            for value in ('true','[]','<<>>','/WrongType'):
                data=fixture(angle=value) if key=='ItalicAngle' else fixture(descriptor='/'+key+' '+value)
                render(data,reject=True)
        for weight in ('0','99','150','1000','-100','1e30'):
            render(fixture(descriptor='/FontWeight '+weight),reject=True)
        for value in ('-1','32.5','2147483648','/Nonsymbolic','[]'):
            render(fixture(flags=value),reject=True)
        for key,body in [('FontWeight','700 400'),('FontFamily','(Arial) (Times)'),('ItalicAngle','-12 0')]:
            data=fixture(angle='8 0 R',extra={8:body}) if key=='ItalicAngle' else fixture(descriptor='/'+key+' 8 0 R',extra={8:body})
            render(data,reject=True)
        for key,value in [('FontWeight','700'),('ItalicAngle','-12'),('FontFamily','(Times New Roman)')]:
            data=fixture(angle='8 0 R',extra={8:value}) if key=='ItalicAngle' else fixture(descriptor='/'+key+' 8 0 R',extra={8:value})
            expected={'FontWeight':'Helvetica-Bold','ItalicAngle':'Helvetica-Oblique','FontFamily':'Times-Roman'}[key]
            assert render(data)==render(fixture(embedded=fonts[expected]));fault(data)
        for angle in (None,'null','8 0 R'):
            assert render(fixture(angle=angle,extra={8:'null'}))==render(fixture(embedded=fonts['Helvetica']))
        for weight in ('null','8 0 R'):
            assert render(fixture(descriptor='/FontWeight '+weight,extra={8:'null'}))==render(fixture(embedded=fonts['Helvetica']))
        assert render(fixture(flags='8 0 R',extra={8:'34'}))==render(fixture(flags=34,embedded=fonts['Times-Roman']))
        # Missing or unsuitable descriptors do not imply a Latin font design.
        bitmap=render(fixture(flags=0))
        for flags in (0,4,36,40,65568,131104):
            assert render(fixture(flags=flags))==bitmap
        assert render(fixture(absent_descriptor=True))==bitmap
        # A valid six-uppercase-letter subset prefix permits a known alias.
        for face,names in ALIASES.items():
            data=fixture(name='ABCDEF+'+names[0],flags=34)
            assert render(data)==render(fixture(flags=34,embedded=fonts[face]))
        for name in ('ABCDE+Arial','ABCDEFG+Arial','abcDEF+Arial','ABC1EF+Arial','ABCDEF+'):
            assert render(fixture(name=name,flags=34))==render(fixture(flags=34,embedded=fonts['Times-Roman']))
        # Existing embedded programs win, including their own glyph identities.
        assert render(fixture(embedded=FONT))==render(fixture(name='CupidEmbedded',embedded=FONT))
        for key in ('FontFile','FontFile2','FontFile3'):
            render(fixture(descriptor='/'+key+' 8 0 R',extra={8:stream('broken','/Subtype /Unsupported')}),reject=True)
        # PDF metrics, spacing and transforms are identical to the chosen embedded program.
        for mode in range(8):
            for subtype in ('Type1','TrueType'):
                content=f'.8 .2 .1 rg 1 w BT /F 24 Tf {mode} Tr 1.1 Tc 2 Tw 75 Tz 2 Ts .9 .2 -.1 1 20 35 Tm [(A) 120 ( B)] TJ ET'
                if mode>=4:content+=' .1 .3 .8 rg 0 0 180 90 re f'
                data=fixture(subtype=subtype,flags=34,descriptor='/FontWeight 700',content=content)
                assert render(data)==render(fixture(subtype=subtype,flags=34,embedded=fonts['Times-Bold'],content=content))
        fault(data)
    print(f'PDF descriptor substitutes: {checks} checks; {allocations} allocation checks')


if __name__=='__main__':main()
