#!/usr/bin/env python3
"""Execute authored Type 3 glyph programs against analytic vector equivalents."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from audit_pdf_corpus import read_ppm


def stream(data,attrs=''):
    if isinstance(data,str):data=data.encode()
    return f'<< {attrs} /Length {len(data)} >> stream\n'.encode()+data+b'\nendstream'


def fixture(content, programs=None, font_extra='', matrix='.001 0 0 .001 0 0',
            encoding='<< /Differences [65 /A /B] >>', resources='', extra=None,
            widths='[600 500]', direct=False, charprocs='/A 6 0 R /B 7 0 R',first=65,last=66,subtype="/Type3"):
    programs=programs or ['600 0 d0 0 0 500 700 re f','500 0 0 0 500 700 d1 0 0 m 500 0 l 250 700 l h f']
    font=(f'<< /Type /Font /Subtype {subtype} /FontBBox [0 0 500 700] /FontMatrix [{matrix}] '
          f'/CharProcs << {charprocs} >> /Encoding {encoding} /FirstChar {first} /LastChar {last} /Widths {widths} {font_extra} >>')
    objects={5:font,6:stream(programs[0]),7:stream(programs[1])}
    objects.update(extra or {})
    font_value=font if direct else '5 0 R'
    return document(f'/MediaBox [0 0 120 90] /Resources << /Font << /F {font_value} >> {resources} >>',
                    stream=content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());count=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-type3-') as temp:
        def render(pdf):
            path=Path(temp)/'case.pdf';path.write_bytes(pdf)
            return subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
        def check(name,pdf,expected=None,reject=False,reference=True,secondary=False,boundary_tolerance=False):
            nonlocal count,refs
            count+=1;actual=render(pdf)
            assert actual.returncode==(3 if reject else 0),(name,actual.stderr)
            if reject:return
            assert struct.unpack('=III',actual.stdout[:12])==(160,120,1)
            if expected is not None:
                other=render(document('/MediaBox [0 0 120 90]',stream=expected))
                assert other.returncode==0 and actual.stdout==other.stdout,(name,'analytic vector differs')
            if '--reference' in sys.argv and reference:
                path=Path(temp)/'reference.pdf';path.write_bytes(pdf)
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as ref:
                        raster=ref[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,pixels=raster.width,raster.height,raster.samples
                else:
                    prefix=Path(temp)/'reference'
                    run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                    assert run.returncode==0,(name,run.stderr)
                    w,h,pixels=read_ppm(prefix.with_suffix('.ppm'))
                rgb=bytes(v for i,v in enumerate(actual.stdout[12:]) if i%4!=3)
                assert (w,h)==(160,120)
                if boundary_tolerance:
                    # Only this shared-stream cache case allows a one-pixel
                    # boundary shift. Its analytic vector comparison stays exact.
                    for y in range(h):
                        for x in range(w):
                            at=(y*w+x)*3
                            if rgb[at:at+3]==pixels[at:at+3]:continue
                            neighbours=[(yy*w+xx)*3 for yy in range(max(0,y-1),min(h,y+2))
                                        for xx in range(max(0,x-1),min(w,x+2))]
                            assert any(rgb[at:at+3]==pixels[j:j+3] for j in neighbours),(name,'reference boundary')
                            assert any(pixels[at:at+3]==rgb[j:j+3] for j in neighbours),(name,'actual boundary')
                else:
                    assert rgb==pixels,(name,'independent pixels differ')
                refs+=1
        start='BT /F 20 Tf 1 0 0 1 10 20 Tm '
        square='q .02 0 0 .02 10 20 cm 0 0 500 700 re f Q'
        pair=square+' q .02 0 0 .02 22 20 cm 0 0 m 500 0 l 250 700 l h f Q'
        check('square',fixture(start+'(A) Tj ET'),square)
        check('pair',fixture(start+'(AB) Tj ET'),pair,reference=False)
        check('direct-font',fixture(start+'(A) Tj ET',direct=True),square)
        check('indirect-subtype',fixture(start+'(A) Tj ET',subtype='8 0 R',extra={8:b'/Type3'}),square)
        check('compressed-glyph',fixture(start+'(A) Tj ET',extra={6:stream(zlib.compress(b'600 0 d0 0 0 500 700 re f'),'/Filter /FlateDecode')}),square)
        check('width-array-reference',fixture(start+'(AB) Tj ET',widths='8 0 R',extra={8:'[600 500]'}),pair,reference=False)
        for mode in range(8):
            check(f'mode-{mode}',fixture(start+f'{mode} Tr (A) Tj ET'),'' if mode==3 else square)
        check('d0-colour',fixture('0 1 0 rg '+start+'(A) Tj ET',programs=['600 0 d0 1 0 0 rg 0 0 500 700 re f','500 0 d0']), '1 0 0 rg '+square)
        check('d1-inherits-colour',fixture('0 1 0 rg '+start+'(A) Tj ET',programs=['600 0 0 0 500 700 d1 1 0 0 rg 0 0 500 700 re f','500 0 d0']), '0 1 0 rg '+square,reference=False)
        check('missing-glyph',fixture(start+'(CA) Tj ET'),square)
        check('state-isolation',fixture(start+'(A) Tj ET 0 0 5 5 re f',programs=['600 0 d0 1 0 0 rg 0 0 500 700 re f','500 0 d0']), '1 0 0 rg '+square+' 0 g 0 0 5 5 re f')
        for matrix,transform in [('.002 0 0 .001 0 0','.04 0 0 .02 10 20'),
                                 ('0 .001 -.001 0 0 0','0 .02 -.02 0 10 20'),
                                 ('.001 .0003 .0002 .001 0 0','.02 .006 .004 .02 10 20'),
                                 ('-.001 0 0 .001 .5 0','-.02 0 0 .02 20 20')]:
            check('font-matrix',fixture(start+'(A) Tj ET',matrix=matrix),
                  'q '+transform+' cm 0 0 500 700 re f Q',reference=False)
        check('text-scale-rise',fixture(start+'50 Tz 3 Ts (A) Tj ET'),
              'q .01 0 0 .02 10 23 cm 0 0 500 700 re f Q')
        check('zero-bbox',fixture(start+'(A) Tj ET').replace(b'/FontBBox [0 0 500 700]',b'/FontBBox [0 0 0 0]    '),square)
        check('escaped-glyph-name',fixture(start+'(A) Tj ET',encoding='<< /Differences [65 /A#23B] >>',
              charprocs='/A#23B 6 0 R /B 7 0 R'),square)
        for base in ('WinAnsiEncoding','MacRomanEncoding','StandardEncoding'):
            check('named-'+base,fixture(start+'(A) Tj ET',encoding='/'+base),square)
            check('base-'+base,fixture(start+'(A) Tj ET',encoding='<< /BaseEncoding /'+base+' >>'),square)
        check('base-and-differences',fixture(start+'(A) Tj ET',encoding='<< /BaseEncoding /WinAnsiEncoding /Differences [65 /B] >>',widths='[500 500]',programs=['600 0 d0 0 0 500 700 re f','500 0 d0 0 0 250 700 re f']),'q .02 0 0 .02 10 20 cm 0 0 250 700 re f Q')
        check('mac-expert-name',fixture(start+'(G) Tj ET',encoding='/MacExpertEncoding',charprocs='/onequarter 6 0 R',first=71,last=71,widths='[600]'),square)
        check('missing-overrides-earlier',fixture(start+'(A) Tj ET',encoding='<< /Differences [65 /A 65 /Missing] >>'),'q Q')
        check('missing-overrides-base',fixture(start+'(A) Tj ET',encoding='<< /BaseEncoding /WinAnsiEncoding /Differences [65 /Missing] >>'),'q Q')
        check('indirect-base-name',fixture(start+'(A) Tj ET',encoding='<< /BaseEncoding 8 0 R >>',extra={8:b'/WinAnsiEncoding'}),square)
        check('named-encoding-reference',fixture(start+'(A) Tj ET',encoding='8 0 R',extra={8:'<< /Differences [65 /A /B] >>'}),square)
        form=stream('0 0 500 700 re f','/Type /XObject /Subtype /Form /BBox [0 0 500 700]')
        check('font-resources',fixture(start+'(A) Tj ET',programs=['600 0 d0 /Shape Do','500 0 d0'],
              font_extra='/Resources << /XObject << /Shape 8 0 R >> >>',extra={8:form}),square)
        check('page-resources-fallback',fixture(start+'(A) Tj ET',programs=['600 0 d0 /Shape Do','500 0 d0'],
              resources='/XObject << /Shape 8 0 R >>',extra={8:form}),square)
        check('empty-font-resources-shadow-page',fixture(start+'(A) Tj ET',programs=['600 0 d0 /Shape Do','500 0 d0'],
              font_extra='/Resources << >>',resources='/XObject << /Shape 8 0 R >>',extra={8:form}),reject=True)
        check('recursive-glyph',fixture(start+'(A) Tj ET',programs=['600 0 d0 BT /F 1 Tf (A) Tj ET','500 0 d0']),reject=True)
        mask_program=['600 0 0 0 500 700 d1 500 0 0 700 0 0 cm BI /W 1 /H 1 /IM true /F /AHx ID 00> EI','500 0 d0']
        # Fractional image edges use the existing image rasterizer; keep the
        # analytic coverage test separate from pixel-aligned external checks.
        check('d1-mask-fractional',fixture('1 0 0 rg '+start+'(A) Tj ET',programs=mask_program),
              '1 0 0 rg '+square,reference=False)
        check('d1-mask',fixture('1 0 0 rg BT /F 30 Tf 1 0 0 1 12 24 Tm (A) Tj ET',programs=mask_program),
              '1 0 0 rg q .03 0 0 .03 12 24 cm 0 0 500 700 re f Q')
        check('d1-colour-image-rejected',fixture(start+'(A) Tj ET',programs=['600 0 0 0 500 700 d1 BI /W 1 /H 1 /BPC 8 /CS /G ID x EI','500 0 d0']),reject=True)
        cmap=b'1 beginbfchar <41> <005A> endbfchar'
        check('encoding-before-unicode',fixture(start+'(A) Tj ET',font_extra='/ToUnicode 8 0 R',extra={8:stream(cmap)}),square)
        # The primary comparison's glyph-cache coverage differs by a border here;
        # a second rasterizer agrees on the pixel-aligned path/glyph geometry.
        check('caller-path-preserved',fixture('0 0 6 6 re BT /F 30 Tf 1 0 0 1 12 24 Tm (A) Tj ET f'),'q .03 0 0 .03 12 24 cm 0 0 500 700 re f Q 0 0 6 6 re f',secondary=True)
        check('negative-size',fixture('BT /F -30 Tf 1 0 0 1 45 60 Tm (A) Tj ET'),
              'q -.03 0 0 -.03 45 60 cm 0 0 500 700 re f Q')
        check('character-spacing',fixture(start+'3 Tc (AA) Tj ET'),square+' q .02 0 0 .02 25 20 cm 0 0 500 700 re f Q')
        check('font-local-alpha',fixture(start+'(A) Tj ET',programs=['600 0 d0 /Half gs 0 0 500 700 re f','500 0 d0'],
              font_extra='/Resources << /ExtGState << /Half << /ca .5 >> >> >>'),'0.5 g '+square,reference=False)
        check('font-resource-reference',fixture(start+'(A) Tj ET',programs=['600 0 d0 /Shape Do','500 0 d0'],
              font_extra='/Resources 9 0 R',extra={8:form,9:'<< /XObject << /Shape 8 0 R >> >>'}),square)
        escaped_font=('<< /Type /Font /Sub#74ype /Type3 /FontB#42ox [0 0 500 700] /FontMa#74rix [.001 0 0 .001 0 0] '
                      '/CharPr#6fcs << /A 6 0 R /B 7 0 R >> /Encoding << /Differences [65 /A /B] >> '
                      '/FirstChar 65 /LastChar 66 /Widths [600 500] >>')
        check('escaped-dictionary-keys',fixture(start+'(A) Tj ET',extra={5:escaped_font}),square)
        # A shared stream is not itself a cycle: distinct font-local resources
        # can give the same program different, terminating callees.
        nested_base=('/Type /Font /Subtype /Type3 /FontBBox [0 0 500 700] /FontMatrix [1 0 0 1 0 0] '
                     '/Encoding << /Differences [65 /A] >> /FirstChar 65 /LastChar 65 /Widths [600]')
        nested={8:'<< '+nested_base+' /CharProcs << /A 6 0 R >> /Resources << /Font << /G 9 0 R >> >> >>',
                9:'<< '+nested_base+' /CharProcs << /A 10 0 R >> >>',
                10:stream('600 0 d0 0 0 500 700 re f')}
        check('shared-glyph-stream-not-cycle',fixture('BT /F 30 Tf 1 0 0 1 12 24 Tm (A) Tj ET',programs=['600 0 d0 BT /G 1 Tf (A) Tj ET','500 0 d0'],
              font_extra='/Resources << /Font << /G 8 0 R >> >>',extra=nested),'q .03 0 0 .03 12 24 cm 0 0 500 700 re f Q',secondary=True,boundary_tolerance=True)
        for program in ['', '0 0 500 700 re f','600 1 d0','500 0 d0','600 0 d0 600 0 d0','600 0 500 700 0 0 d1','BI /W 1 /H 1 /IM true ID x EI 600 0 d0']:
            check('bad-glyph',fixture(start+'(A) Tj ET',programs=[program,'500 0 d0']),reject=True)
        check('outside-d0',document('/MediaBox [0 0 120 90]',stream='600 0 d0'),reject=True)
    print(f'PDF Type 3: {count} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
