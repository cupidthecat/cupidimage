#!/usr/bin/env python3
"""Check text-state composition independently of the fallback glyph design."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_pages import document


def main():
    probe=str(Path(sys.argv[1]).resolve())
    count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-text-state-') as temp:
        def render(content):
            path=Path(temp)/'case.pdf'
            path.write_bytes(document('/MediaBox [0 0 120 90]',stream=content))
            return subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
        def same(label,a,b):
            nonlocal count
            x,y=render(a),render(b)
            assert x.returncode==y.returncode==0,(label,x.stderr,y.stderr)
            assert x.stdout==y.stdout,(label,'pixels differ')
            assert any(v!=255 for v in x.stdout[12:]),(label,'empty comparison')
            count+=1
        matrices=[(1,0,0,1,20,40),(0,1,-1,0,65,20),(1,.3,.2,1,20,30),
                  (-1,0,0,1,90,40),(1,0,0,-1,20,65),(1.5,0,0,.75,20,40)]
        for matrix in matrices:
            m=' '.join(map(str,matrix))
            same('Tm vs CTM',f'BT /F 12 Tf {m} Tm (AB) Tj ET',
                 f'q {m} cm BT /F 12 Tf (AB) Tj ET Q')
            a,b,c,d,e,f=matrix
            translated=' '.join(map(str,(a,b,c,d,e+7*a-5*c,f+7*b-5*d)))
            same('Td composition',f'BT /F 12 Tf {m} Tm 7 -5 Td (AB) Tj ET',
                 f'BT /F 12 Tf {translated} Tm (AB) Tj ET')
            same('TD and next line',f'BT /F 12 Tf {m} Tm 7 -5 TD (AB) Tj T* (C) Tj ET',
                 f'BT /F 12 Tf {m} Tm 5 TL 7 -5 Td (AB) Tj 0 -5 Td (C) Tj ET')
            same('rise',f'BT /F 12 Tf {m} Tm 3 Ts (AB) Tj ET',
                 f'BT /F 12 Tf {a} {b} {c} {d} {e+3*c} {f+3*d} Tm (AB) Tj ET')
        prefix='BT /F 12 Tf 1 0 0 1 20 40 Tm '
        same('Tc',prefix+'3 Tc (AB) Tj ET',prefix+'[(A) -250 (B)] TJ ET')
        same('Tw',prefix+'3 Tw (A B) Tj ET',prefix+'[(A ) -250 (B)] TJ ET')
        same('TJ and horizontal scale',prefix+'50 Tz [(A) 250 (B)] TJ ET',
             'BT /F 12 Tf .5 0 0 1 20 40 Tm [(A) 250 (B)] TJ ET')
        for scale in (0,50,150,-100):
            # Keep a visible final path even when the text collapses to zero width.
            same('Tz',prefix+f'{scale} Tz (AB) Tj ET 0 0 1 1 re f',
                 f'BT /F 12 Tf {scale/100} 0 0 1 20 40 Tm (AB) Tj ET 0 0 1 1 re f')
        same('quote',prefix+"10 TL (A) Tj (B) ' ET",prefix+'10 TL (A) Tj T* (B) Tj ET')
        same('double quote',prefix+'10 TL 3 2 (A B) " ET',prefix+'10 TL 3 Tw 2 Tc T* (A B) Tj ET')
        same('zero leading',prefix+"(A) Tj (B) ' ET",prefix+'(A) Tj 0 0 Td (B) Tj ET')
        same('negative leading',prefix+"-10 TL (A) Tj (B) ' ET",prefix+'(A) Tj 0 10 Td (B) Tj ET')
        same('q Q text parameters',prefix+'3 Tc q /F 24 Tf 2 Tw 50 Tz 5 Ts 9 TL 3 Tr Q (AB) Tj ET',prefix+'3 Tc (AB) Tj ET')
        same('BT retains state','3 Tc 50 Tz 4 Ts '+prefix+'(AB) Tj ET',prefix+'3 Tc 50 Tz 4 Ts (AB) Tj ET')
        same('BT resets matrices',prefix+'(A) Tj ET BT /F 12 Tf 20 40 Td (B) Tj ET',prefix+'(A) Tj ET '+prefix+'(B) Tj ET')
        same('invisible advances',prefix+'3 Tr (A) Tj 0 Tr (B) Tj ET',prefix+'[-667] TJ (B) Tj ET')
        same('control byte is not newline',prefix+'(A\\012B) Tj ET',prefix+'[(A) -500 (B)] TJ ET')
        same('zero font size',prefix+'/F 0 Tf (AB) Tj ET 0 0 1 1 re f','0 0 1 1 re f')
        for mode in (0,1,2):
            same('clip mode also paints',prefix+f'{mode} Tr (AB) Tj ET',prefix+f'{mode+4} Tr (AB) Tj ET')
        same('text clip fill',prefix+'7 Tr (AB) Tj ET 1 0 0 rg 0 0 120 90 re f',
             '1 0 0 rg '+prefix+'(AB) Tj ET')
        same('text clip unions shows',prefix+'7 Tr (A) Tj (B) Tj ET 1 0 0 rg 0 0 120 90 re f',
             '1 0 0 rg '+prefix+'(AB) Tj ET')
        same('clip intersects path','0 40 120 8 re W n '+prefix+'7 Tr (AB) Tj ET 1 0 0 rg 0 0 120 90 re f',
             '0 40 120 8 re W n 1 0 0 rg '+prefix+'(AB) Tj ET')
        same('text clip restored by Q','q '+prefix+'7 Tr (AB) Tj ET Q 1 0 0 rg 0 0 120 90 re f',
             '1 0 0 rg 0 0 120 90 re f')
        blank=render(prefix+'7 Tr ( ) Tj ET 0 0 120 90 re f')
        # Outline-less glyphs do not establish a new clip (ISO 32000-1/2 9.3.6).
        expected=render('0 0 120 90 re f')
        assert blank.returncode==0 and blank.stdout==expected.stdout
        count+=1
        stroke=render('1 0 0 RG '+prefix+'1 Tr (AB) Tj ET')
        assert stroke.returncode==0 and any(stroke.stdout[i:i+4]==bytes([255,0,0,255]) for i in range(12,len(stroke.stdout),4))
        assert not any(stroke.stdout[i:i+4]==bytes([0,0,0,255]) for i in range(12,len(stroke.stdout),4))
        count+=1
        same('reflected fallback symmetry',prefix+'(H) Tj ET',
             'BT /F 12 Tf -1 0 0 1 24.5 40 Tm (H) Tj ET')
        canceled=render(prefix+'7 Tr (H) Tj -1 0 0 1 24.5 40 Tm (H) Tj ET 0 0 120 90 re f')
        assert canceled.returncode==0 and all(v==255 for v in canceled.stdout[12:])
        count+=1
        # Widths are indexed by encoded character codes, not Unicode output.
        for indirect in (False,True):
            for width in (0,250,600,1000,-100):
                widths='6 0 R' if indirect else f'[{width} 700]'
                objects={5:f'<< /Type /Font /Subtype /Type1 /FirstChar 65 /LastChar 66 /Widths {widths} >>',6:f'[{width} 700]'}
                path=Path(temp)/'widths.pdf'
                path.write_bytes(document('/MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> >>',stream=prefix+'(AB) Tj ET',extra=objects))
                actual=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
                expected=render(prefix+f'(A) Tj 1 0 0 1 {20+width*12/1000} 40 Tm (B) Tj ET')
                assert actual.returncode==expected.returncode==0 and actual.stdout==expected.stdout,('width',indirect,width)
                count+=1
        objects={5:'<< /Type /Font /Subtype /Type1 /FirstChar 65 /LastChar 65 /Widths [600] /FontDescriptor 6 0 R >>',6:'<< /MissingWidth 250 >>'}
        path=Path(temp)/'missing-width.pdf'
        path.write_bytes(document('/MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> >>',stream=prefix+'(BA) Tj ET',extra=objects))
        actual=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
        expected=render(prefix+'(B) Tj 1 0 0 1 23 40 Tm (A) Tj ET')
        assert actual.returncode==expected.returncode==0 and actual.stdout==expected.stdout
        count+=1
        # ToUnicode changes glyph selection, but not which character code gets
        # word spacing or which entry supplies its advance width.
        for source,mapped,text,width,expected_x,expected_prefix in [
            ('20','0041','2042',250,26,'(A) Tj'),
            ('41','0020','4142',600,27.2,''),
            ('0020','0041','002042',None,28.004,'(A) Tj')]:
            cmap=f'1 beginbfchar <{source}> <{mapped}> endbfchar'.encode()
            code=int(source,16)
            metrics=f'/FirstChar {code} /LastChar {code} /Widths [{width}]' if width is not None else '/Subtype /Type0'
            objects={5:f'<< /Type /Font {metrics} /ToUnicode 7 0 R >>',
                     7:b'<< /Length '+str(len(cmap)).encode()+b' >> stream\n'+cmap+b'\nendstream'}
            path=Path(temp)/'mapped-width.pdf'
            path.write_bytes(document('/MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> >>',
                                      stream=prefix+f'3 Tw <{text}> Tj ET',extra=objects))
            actual=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            expected=render(prefix+expected_prefix+f' 1 0 0 1 {expected_x} 40 Tm (B) Tj ET')
            assert actual.returncode==expected.returncode==0 and actual.stdout==expected.stdout,('mapped width',source)
            count+=1
        same('negative font size','BT /F -12 Tf 1 0 0 1 90 40 Tm (AB) Tj ET',
             'BT /F 12 Tf -1 0 0 -1 90 40 Tm (AB) Tj ET')
        same('Q does not restore text matrices',prefix+'q 1 0 0 1 50 40 Tm Q (AB) Tj ET',
             'BT /F 12 Tf 1 0 0 1 50 40 Tm (AB) Tj ET')
        for body in ['1 0 0 (bad) 20 40 Tm','/F (bad) Tf','(bad) Tc','8 Tr','-1 Tr','2.5 Tr','(bad) Ts','BT']:
            result=render(prefix+body+' (A) Tj ET')
            assert result.returncode==3 and result.stderr,(body,result.returncode,result.stderr)
            count+=1
    print(f'PDF text state: {count} checks passed')


if __name__=='__main__':
    main()
