#!/usr/bin/env python3
"""Check group-derived alpha/luminosity masks, transfer functions and state lifetime."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm

A='0.5019607843137255'


def fixture(mask_program='0 0 8.8 12 re f',content='/M gs 1 0 0 rg 0 0 18 12 re f',subtype='Alpha',space='/DeviceRGB',isolated='true',mask_extra='',form_extra='',state_extra='',extra=None,page_extra='',states_extra=''):
    group='<< /S /Transparency '+('/CS '+space if space else '')+' /I '+isolated+' >>'
    objects={5:form(mask_program,'/BBox [0 0 18 12] /Group '+group+' '+form_extra)}
    objects.update(extra or {})
    states='/ExtGState << /M << /SMask << /S /'+subtype+' /G 5 0 R '+mask_extra+' >> '+state_extra+' >> /Clear << /SMask /None >> /Null << /SMask null >> /A << /ca '+A+' /CA '+A+' >> '+states_extra+' >>'
    return document('/MediaBox [0 0 18 12] /Resources << '+states+' '+page_extra+' >>',stream=content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    white=(255,255,255);red=(255,0,0);blue=(0,0,255)
    with tempfile.TemporaryDirectory(prefix='cupidimage-soft-masks-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,pdf,expected=None,reject=False,reference=True,tolerance=1):
            nonlocal checks,refs
            path.write_bytes(pdf);result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert result.returncode==(3 if reject else 0),(name,result.returncode,result.stderr)
            checks+=1
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1)
            if expected:
                want=bytes(v for y in range(16) for x in range(24) for v in (*expected(x,y),255))
                assert result.stdout[12:]==want,(name,'analytic differs',next((i,a,b) for i,(a,b) in enumerate(zip(result.stdout[12:],want)) if a!=b))
            if '--reference' in sys.argv and reference:
                prefix=Path(tmp)/'reference';r=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                assert r.returncode==0,r.stderr;w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(result.stdout[12:]) if i%4!=3)
                assert (w,h)==(24,16) and max(abs(a-b) for a,b in zip(actual,rgb))<=tolerance,(name,'independent differs',max(abs(a-b) for a,b in zip(actual,rgb)))
                refs+=1
        left=lambda x,y:red if x<12 else white
        check('alpha-ignores-black',fixture(),left)
        check('alpha-gray-group',fixture(space='/DeviceGray'),left)
        check('alpha-ignores-BC',fixture(mask_extra='/BC /Unused'),left)
        check('empty-alpha',fixture(mask_program=''),lambda x,y:white)
        check('alpha-member-opacity',fixture(mask_program='/A gs 0 0 18 12 re f'),lambda x,y:(255,127,127))
        check('caller-opacity-once',fixture(mask_program='0 0 18 12 re f',content='/A gs /M gs 1 0 0 rg 0 0 18 12 re f'),lambda x,y:(255,127,127))
        check('same-state-opacity-once',fixture(mask_program='0 0 18 12 re f',state_extra='/ca '+A),lambda x,y:(255,127,127))
        check('clear',fixture(content='/M gs /Clear gs 1 0 0 rg 0 0 18 12 re f'),lambda x,y:red)
        check('null-keeps-mask',fixture(content='/M gs /Null gs 1 0 0 rg 0 0 18 12 re f'),left)
        check('save-restore',fixture(content='1 0 0 rg /M gs q /Clear gs 0 0 1 rg 0 0 18 12 re f Q 0 0 18 12 re f'),lambda x,y:red if x<12 else blue)
        second=form('9 0 9 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceRGB >>')
        state='/M2 << /SMask << /S /Alpha /G 6 0 R >> >>'
        check('replacement-not-intersection',fixture(content='/M gs /M2 gs 1 0 0 rg 0 0 18 12 re f',states_extra=state,extra={6:second}),lambda x,y:white if x<12 else red)
        check('restore-previous-mask',fixture(content='/M gs q /M2 gs Q 1 0 0 rg 0 0 18 12 re f',states_extra=state,extra={6:second}),left)
        second=form('/M gs 0 0 18 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceRGB >>')
        check('explicit-nested-mask',fixture(content='/M2 gs 1 0 0 rg 0 0 18 12 re f',states_extra=state,extra={6:second}),left)
        check('matrix-captured',fixture(mask_program='0 0 5.8 12 re f',content='1 0 0 1 6 0 cm /M gs 1 0 0 1 -6 0 cm 1 0 0 rg 0 0 18 12 re f'),lambda x,y:red if 8<=x<16 else white)
        check('group-matrix',fixture(mask_program='0 0 5.8 12 re f',form_extra='/Matrix [1 0 0 1 6 0]'),lambda x,y:red if 8<=x<16 else white)
        for space in ('/DeviceRGB','/DeviceGray'):
            check('luminosity-black-'+space,fixture(subtype='Luminosity',space=space),lambda x,y:white)
            check('luminosity-white-'+space,fixture(mask_program='1 g 0 0 8.8 12 re f',subtype='Luminosity',space=space),left)
            bc='[1]' if space=='/DeviceGray' else '[1 1 1]'
            check('luminosity-backdrop-'+space,fixture(subtype='Luminosity',space=space,mask_extra='/BC '+bc),lambda x,y:white if x<12 else red)
            for isolated in ('true','false'):
                check('luminosity-opacity-'+space+isolated,fixture(mask_program='/A gs 1 g 0 0 18 12 re f',subtype='Luminosity',space=space,isolated=isolated),lambda x,y:(255,127,127))
        for space in ('/DeviceRGB','/DeviceGray'):
            check('luminosity-red-'+space,fixture(mask_program='1 0 0 rg 0 0 18 12 re f',subtype='Luminosity',space=space),lambda x,y:(255,179,179))
        for isolated in ('true','false'):
            expected=(255,179,179) if isolated=='true' else white
            check('luminosity-backdrop-blend-'+isolated,fixture(mask_program='/Mul gs 1 0 0 rg 0 0 18 12 re f',subtype='Luminosity',isolated=isolated,mask_extra='/BC [0 1 0]',form_extra='/Resources << /ExtGState << /Mul << /BM /Multiply >> >> >>'),lambda x,y:expected,reference=isolated=='true')
        check('null-transfer',fixture(mask_extra='/TR null'),left)
        check('clamped-transfer',fixture(mask_extra='/TR << /FunctionType 2 /Domain [0 1] /C0 [-1] /C1 [2] /N 1 >>'),left,reference=False)
        check('indirect-mask',fixture(content='/Indirect gs 1 0 0 rg 0 0 18 12 re f',extra={6:b'<< /SMask 7 0 R >>',7:b'<< /S /Alpha /G 5 0 R >>',3:b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << /ExtGState << /Indirect 6 0 R >> >> >>'}),left)
        type3=b'<< /Type /Font /Subtype /Type3 /FontBBox [0 0 18 12] /FontMatrix [1 0 0 1 0 0] /Encoding << /Differences [65 /A] >> /FirstChar 65 /LastChar 65 /Widths [18] /CharProcs << /A 8 0 R >> >>'
        check('type3-inherits-mask',fixture(content='/M gs 1 0 0 rg BT /F 1 Tf (A) Tj ET',extra={7:type3,8:stream('18 0 d0 0 0 18 12 re f')},page_extra='/Font << /F 7 0 R >>'),left)
        image=stream(bytes([255,0,0]),'/Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 7 0 R')
        image_mask=stream(bytes([128]),'/Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8')
        check('image-mask-overrides-graphics-mask',fixture(mask_program='/A gs 0 0 18 12 re f',content='/M gs 18 0 0 12 0 0 cm /Im Do',extra={6:image,7:image_mask},page_extra='/XObject << /Im 6 0 R >>'),lambda x,y:(255,127,127))
        check('image-mask-does-not-clear-state',fixture(mask_program='/A gs 0 0 18 12 re f',content='/M gs 18 0 0 12 0 0 cm /Im Do .05555555555555556 0 0 .08333333333333333 0 0 cm 0 0 1 rg 0 0 18 12 re f',extra={6:image,7:image_mask},page_extra='/XObject << /Im 6 0 R >>'),lambda x,y:(127,63,191),reference=False)
        keyed=stream(bytes([255,0,0]),'/Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Mask [0 0 0 0 0 0]')
        check('color-key-overrides-graphics-mask',fixture(mask_program='/A gs 0 0 18 12 re f',content='/M gs 18 0 0 12 0 0 cm /Im Do',extra={6:keyed},page_extra='/XObject << /Im 6 0 R >>'),lambda x,y:red,reference=False)
        pattern=stream('1 0 0 rg 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 6 /YStep 6 /Resources << >>')
        check('pattern-not-double-masked',fixture(mask_program='/A gs 0 0 18 12 re f',content='/M gs /Pattern cs /P scn 0 0 18 12 re f',extra={6:pattern},page_extra='/Pattern << /P 6 0 R >>'),lambda x,y:(255,127,127) if x%8<4 and y%8>=4 else white)
        check('pattern-defines-alpha',fixture(mask_program='/Pattern cs /P scn 0 0 18 12 re f',form_extra='/Resources << /Pattern << /P 6 0 R >> >>',extra={6:pattern}),lambda x,y:red if x%8<4 and y%8>=4 else white)
        check('identity-transfer',fixture(mask_extra='/TR /Identity'),left)
        inverse='<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>'
        check('inverse-including-outside',fixture(mask_extra='/TR '+inverse),lambda x,y:white if x<12 else red)
        check('gray-luminosity-transfer',fixture(mask_program=A+' g 0 0 8.8 12 re f',subtype='Luminosity',space='/DeviceGray',mask_extra='/BC [1] /TR '+inverse),lambda x,y:(255,128,128) if x<12 else white)
        check('sampled-transfer',fixture(mask_extra='/TR 6 0 R',extra={6:stream(bytes([255,0]),'/FunctionType 0 /Domain [0 1] /Range [0 1] /Size [2] /BitsPerSample 8')}),lambda x,y:white if x<12 else red)
        stitched='<< /FunctionType 3 /Domain [0 1] /Functions ['+inverse+'] /Bounds [] /Encode [0 1] >>'
        check('stitched-transfer',fixture(mask_extra='/TR '+stitched),lambda x,y:white if x<12 else red)
        check('calculator-transfer',fixture(mask_extra='/TR 6 0 R',extra={6:stream('{ 1 exch sub }','/FunctionType 4 /Domain [0 1] /Range [0 1]')}),lambda x,y:white if x<12 else red)
        paint=form('1 0 0 rg 0 0 11.8 12 re f 0 0 1 rg 6 0 12 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /I true >>')
        check('mask-applied-once-to-group',fixture(mask_program='/A gs 0 0 18 12 re f',content='/M gs /Paint Do',extra={6:paint},page_extra='/XObject << /Paint 6 0 R >>'),lambda x,y:(255,127,127) if x<8 else (127,127,255))
        check('fill-stroke-single-mask-application',fixture(mask_program='/A gs 0 0 18 12 re f',content='/M gs 1 0 0 rg 1 0 0 RG 2.8 w 3 3 12 6 re B'),lambda x,y:(255,127,127) if 2<=x<22 and 2<=y<14 else white,reference=False)
        check('ordinary-form-inherits',fixture(content='/M gs /Paint Do',extra={6:form('1 0 0 rg 0 0 18 12 re f','/BBox [0 0 18 12]')},page_extra='/XObject << /Paint 6 0 R >>'),left)
        uncolored=stream('/Local gs 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 3 3] /XStep 6 /YStep 6 /Resources << /ExtGState << /Local << /SMask << /S /Luminosity /G 7 0 R >> >> >> >>')
        mask_form=form('1 g 0 0 6 6 re f','/BBox [0 0 6 6] /Group << /S /Transparency /I true /CS /DeviceGray >>')
        check('mask-colours-independent-of-uncolored-pattern',fixture(content='/PC cs 1 0 0 /P scn 0 0 18 12 re f',extra={6:uncolored,7:mask_form},page_extra='/Pattern << /P 6 0 R >> /ColorSpace << /PC [/Pattern /DeviceRGB] >>'),lambda x,y:red if x%8<4 and y%8>=4 else white)
        mask_form=form('/Blend gs 1 g 0 0 6 6 re f','/BBox [0 0 6 6] /Group << /S /Transparency /I true /CS /DeviceGray >> /Resources << /ExtGState << /Blend << /BM /Screen >> >> >>')
        check('mask-blending-inside-pattern',fixture(content='/PC cs 1 0 0 /P scn 0 0 18 12 re f',extra={6:uncolored,7:mask_form},page_extra='/Pattern << /P 6 0 R >> /ColorSpace << /PC [/Pattern /DeviceRGB] >>'),lambda x,y:red if x%8<4 and y%8>=4 else white)
        for kwargs in ({'subtype':'Other'},{'subtype':'Luminosity','space':''},{'subtype':'Luminosity','mask_extra':'/BC [1 1]'}, {'mask_extra':'/TR /Unknown'},{'mask_program':'/M gs 0 0 18 12 re f'},{'space':'/DeviceCMYK'},{'extra':{5:form('0 0 18 12 re f','/BBox [0 0 18 12]')}}):
            check('invalid',fixture(**kwargs),reject=True)
    print(f'Graphics-state soft masks: {checks} checks passed; {refs} independent comparisons')

if __name__=='__main__':main()
