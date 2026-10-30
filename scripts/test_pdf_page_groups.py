#!/usr/bin/env python3
"""Check isolated page compositing and page-local blending-space selection."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_transparency import MODES
from audit_pdf_corpus import read_ppm


def fixture(content, group='', resources='', extra=None, parent=''):
    return document('/MediaBox [0 0 18 12] '+('/Group '+group if group else '')+
                    ' /Resources << '+resources+' >>', stream=content,
                    parent_attrs=parent, extra=extra)


def main():
    probe=str(Path(sys.argv[1]).resolve())
    checks=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-page-groups-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,pdf,expected=None,reject=False,reference=True):
            nonlocal checks,refs
            path.write_bytes(pdf)
            result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert result.returncode==(3 if reject else 0),(name,result.returncode,result.stderr)
            checks+=1
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1)
            wanted=bytes(v for y in range(16) for x in range(24) for v in (*expected(x,y),255))
            assert result.stdout[12:]==wanted,(name,'analytic differs',next((i,a,b) for i,(a,b) in enumerate(zip(result.stdout[12:],wanted)) if a!=b))
            if '--reference' in sys.argv and reference:
                prefix=Path(tmp)/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                assert run.returncode==0,run.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(result.stdout[12:]) if i%4!=3)
                assert (w,h)==(24,16) and max(abs(a-b) for a,b in zip(actual,rgb))<=1,(name,'independent differs',max(abs(a-b) for a,b in zip(actual,rgb)))
                refs+=1
        red=lambda x,y:(255,0,0)
        white=lambda x,y:(255,255,255)
        for group in ('','null','<< /S /Transparency >>','<< /S /Transparency /I false /CS /DeviceRGB >>','<< /S /Transparency /I true /CS /DeviceRGB >>'):
            for mode in MODES:
                resources='/ExtGState << /G << /BM /'+mode+' /ca .5 >> >>'
                check('initial-'+group+'-'+mode,fixture('/G gs 1 0 0 rg 0 0 18 12 re f',group,resources),lambda x,y:(255,128,128))
        gray='<< /S /Transparency /I false /CS /DeviceGray >>'
        check('gray-native',fixture('.5 g 0 0 18 12 re f',gray),lambda x,y:(128,128,128))
        check('gray-converts-source',fixture('1 0 0 rg 0 0 18 12 re f',gray),lambda x,y:(77,77,77),reference=False)
        check('gray-space-alias',fixture('1 0 0 rg 0 0 18 12 re f','<< /S /Transparency /CS /G >>','/ColorSpace << /G /DeviceGray >>'),reject=True)
        check('gray-space-indirect',fixture('1 0 0 rg 0 0 18 12 re f','5 0 R',extra={5:b'<< /S /Transparency /I 6 0 R /CS 7 0 R >>',6:b'false',7:b'/DeviceGray'}),lambda x,y:(77,77,77),reference=False)
        check('not-inherited',fixture('1 0 0 rg 0 0 18 12 re f',parent='/Group '+gray),red)
        check('final-clip-does-not-reclip',fixture('1 0 0 rg 0 0 18 12 re f 0 0 9 12 re W n'),red)
        check('empty-gray',fixture('',gray),white)
        check('contentless-invalid-group',fixture('', '<< /S /Transparency /CS /DeviceCMYK >>'),reject=True)
        nested=form('1 0 0 rg 0 0 18 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /I false >>')
        check('nested-inherits-gray',fixture('/F Do',gray,'/XObject << /F 5 0 R >>',{5:nested}),lambda x,y:(77,77,77),reference=False)
        for group in ('<< /S /Other >>','<< /S /Transparency /I /Bad >>','<< /S /Transparency /CS /DeviceCMYK >>','<< /S /Transparency /CS [/Indexed /DeviceRGB 0 <000000>] >>'):
            check('unsupported-'+group,fixture('0 0 18 12 re f',group),reject=True)
    print(f'PDF page groups: {checks} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
