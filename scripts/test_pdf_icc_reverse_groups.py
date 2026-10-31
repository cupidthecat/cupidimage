#!/usr/bin/env python3
"""Qualify ICC destination pipelines through PDF group painting and conversion."""
from pathlib import Path
import itertools
import struct
import subprocess
import sys
import tempfile
from test_pdf_icc import profile, curve, gamma, xyz
from test_pdf_icc_lut import profile as table_profile, lut as forward_lut
from test_pdf_icc_reverse import lut, mba, table
from test_pdf_intents import tags
from test_pdf_cie_groups import page, color, FULL
from test_pdf_type3 import stream
from test_pdf_forms import form

D50='0.964202880859375 1 0.8249053955078125'

def main():
    probe,allocator=[str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupid-icc-destination-') as directory:
        path=Path(directory)/'case.pdf'
        def render(data,reject=False):
            nonlocal checks
            path.write_bytes(data);r=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=30)
            assert (r.returncode!=0)==reject,(r.returncode,r.stderr)
            checks+=1
            return r.stdout[12:] if not reject else None
        def fault(data,reject=False):
            nonlocal allocations
            path.write_bytes(data);r=subprocess.run([allocator,str(path),str(int(reject))],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(r.stdout.split('checks')[0].split()[-1])
        def compare(data,n,values,space,expected,intent='RelativeColorimetric'):
            spec=('destination',n,'[/ICCBased 100 0 R]',{100:stream(data,f'/N {n}')},False)
            # CalRGB's identity matrix gives known XYZ values; Lab gives known
            # PCS components. The B2A table then selects the expected native ink.
            content='/'+intent+' ri /Source cs '+' '.join(map(str,values))+' scn '+FULL
            actual_doc=page(spec,content,spaces='/Source '+space)
            actual=render(actual_doc)
            expected_page=render(page(spec,color(expected)+FULL,group=False))
            assert max(abs(a-b) for a,b in zip(actual,expected_page))<=1,(n,intent,values,actual[:4],expected_page[:4])
            return actual_doc,spec
        for n,lab,kind,version in itertools.product((1,3),(False,True),('mft2','mft1','mba'),(2,4)):
            if (kind=='mft1' and not lab) or (kind=='mba' and version==2):continue
            reverse=lut(n,8 if kind=='mft1' else 16) if kind!='mba' else mba(n,table=table(n),a=[curve()]*n)
            # A genuine forward table plus reverse table admits a group even
            # though the forward table cannot be inverted analytically.
            data=tags(table_profile(forward_lut(n),inputs=n,lab=lab,version=version),{b'B2A0':reverse})
            if lab:
                values=(50,0,0);space='[/Lab << /WhitePoint ['+D50+'] >>]'
                factor=65280/65535 if kind=='mft2' else 1
                x=(.5*factor,128/255*factor,128/255*factor)
            else:
                values=(.2,.4,.6);space='[/CalRGB << /WhitePoint ['+D50+'] >>]'
                x=tuple(v*32768/65535 for v in values)
            expected=(x[0],min(x[1],x[2]),min(x))[:n]
            doc,spec=compare(data,n,values,space,expected)
            fault(doc)
            child=form('/Source cs '+' '.join(map(str,values))+' scn '+FULL,
                       '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS '+spec[2]+' >> /Resources << /ColorSpace << /Source '+space+' >> >>')
            actual=render(page(spec,'/G Do',group=False,objects={20:child},resources='/XObject << /G 20 0 R >>'))
            assert max(abs(a-b) for a,b in zip(actual,render(page(spec,color(expected)+FULL,group=False))))<=1
        # Reverse tables take precedence over invertible matrix/TRC metadata.
        reverse=lut(3)
        data=profile([gamma(2)]*3,extra=[(b'B2A0',reverse)])
        space='[/CalRGB << /WhitePoint ['+D50+'] >>]';values=(.2,.4,.6)
        x=tuple(v*32768/65535 for v in values)
        doc,spec=compare(data,3,values,space,(x[0],min(x[1],x[2]),min(x)))
        fault(doc)
        for signature in (b'bad ',b'mAB '):
            malformed=tags(data,{b'B2A0':signature+reverse[4:]})
            bad=('bad',3,spec[2],{100:stream(malformed,'/N 3')},False)
            render(page(bad,color((.5,)*3)+FULL,group=False))
            doc=page(bad,color((.5,)*3)+FULL);render(doc,True);fault(doc,True)
        # Select the destination table independently for each PDF intent.
        choices={b'B2A0':lut(3,output_tables=[[.25,.25]]*3),
                 b'B2A1':lut(3,output_tables=[[.5,.5]]*3),
                 b'B2A2':lut(3,output_tables=[[.75,.75]]*3)}
        selected=tags(profile(),choices)
        for intent,value in (('RelativeColorimetric',.5),('Perceptual',.25),('Saturation',.75),('AbsoluteColorimetric',.5)):
            rounded=round(value*65535)/65535
            compare(selected,3,values,space,(rounded,)*3,intent)
        # Perceptual PCS encoding restores the reference-medium black before
        # evaluating the destination table. Saturation opts in through rig2.
        for intent,prmg in (('Perceptual',False),('Saturation',True),('Saturation',False)):
            extras={b'B2A0':lut(3)}
            if prmg:extras[b'rig2']=b'sig '+bytes(4)+b'prmg'
            selected=tags(profile(),extras)
            black=(.003357,.003479,.002869);white=(0xf6d6/65536,1,0xd32d/65536)
            connection=tuple(v*(1-b/w)+b for v,b,w in zip(values,black,white)) if intent=='Perceptual' or prmg else values
            encoded=tuple(v*32768/65535 for v in connection)
            compare(selected,3,values,space,(encoded[0],min(encoded[1],encoded[2]),min(encoded)),intent)
        # A malformed unused destination intent does not disable a valid one.
        selected=tags(profile(),{**choices,b'B2A2':b'bad '+bytes(16)})
        compare(selected,3,values,space,(round(.5*65535)/65535,)*3)
        bad=('bad saturation',3,spec[2],{100:stream(selected,'/N 3')},False)
        doc=page(bad,'/Saturation ri /Source cs .2 .4 .6 scn '+FULL,spaces='/Source '+space)
        render(doc,True);fault(doc,True)
        # Absolute-colorimetric destination conversion undoes media-white
        # scaling before encoding the PCS input to the reverse table.
        selected=bytearray(tags(profile(),{b'B2A0':lut(3),b'wtpt':xyz((0xf6d6/65536,.5,0xd32d/65536))}))
        selected[12:16]=b'scnr'
        encoded=(values[0]*32768/65535,values[1]*2*32768/65535,values[2]*32768/65535)
        compare(bytes(selected),3,values,space,(encoded[0],min(encoded[1],encoded[2]),min(encoded)),'AbsoluteColorimetric')
        # Explicit reverse tables do not require the forward curves themselves
        # to have mathematical inverses.
        selected=profile([curve([1234,1234])]*3,extra=[(b'B2A0',lut(3))])
        compare(selected,3,values,space,(x[0],min(x[1],x[2]),min(x)))
        # A lookup-based soft-mask group converts painted colors through B2A,
        # then derives luminosity from the forward CIE Y rather than device RGB.
        selected=tags(table_profile(forward_lut(),lab=False),{b'B2A0':lut()})
        spec=('lookup mask',3,'[/ICCBased 100 0 R]',{100:stream(selected,'/N 3')},False)
        child=form('/Source cs .2 .4 .6 scn '+FULL,
                   '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS '+spec[2]+' >> /Resources << /ColorSpace << /Source '+space+' >> >>')
        doc=page(spec,'/Half gs 0 g '+FULL,group=False,objects={20:child,30:'<< /SMask << /S /Luminosity /G 20 0 R >> >>'})
        assert render(doc)==bytes((204,204,204,255))*(24*16)
        fault(doc)
        # Unsupported floating-point destination tags affect destination use only.
        malformed=tags(data,{b'B2D0':b'mpet'+bytes(20)})
        bad=('float destination',3,spec[2],{100:stream(malformed,'/N 3')},False)
        render(page(bad,color((.5,)*3)+FULL,group=False))
        doc=page(bad,color((.5,)*3)+FULL);render(doc,True);fault(doc,True)
    print(f'ICC destination groups: {checks} page checks; {allocations} allocation checks')

if __name__=='__main__':main()
