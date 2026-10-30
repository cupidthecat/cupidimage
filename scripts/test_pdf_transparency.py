#!/usr/bin/env python3
"""Check constant alpha, blend modes, state restoration, and object coverage."""
from pathlib import Path
import math
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_images import image_stream
from audit_pdf_corpus import read_ppm

MODES = ['Normal','Multiply','Screen','Overlay','Darken','Lighten','ColorDodge','ColorBurn',
         'HardLight','SoftLight','Difference','Exclusion','Hue','Saturation','Color','Luminosity']


def lum(c):
    return sum(x*w for x,w in zip(c,(.3,.59,.11)))


def setlum(c,l):
    c=[x+l-lum(c) for x in c]
    n,x=min(c),max(c)
    if n<0:c=[l+(v-l)*l/(l-n) for v in c]
    if x>1:c=[l+(v-l)*(1-l)/(x-l) for v in c]
    return c


def setsat(c,s):
    low,mid,high=sorted(range(3),key=lambda i:c[i])
    result=[0.,0.,0.]
    if c[high]>c[low]:
        result[mid]=(c[mid]-c[low])*s/(c[high]-c[low]);result[high]=s
    return result


def blend(mode,source,back):
    if mode=='Hue':return setlum(setsat(source,max(back)-min(back)),lum(back))
    if mode=='Saturation':return setlum(setsat(back,max(source)-min(source)),lum(back))
    if mode=='Color':return setlum(source,lum(back))
    if mode=='Luminosity':return setlum(back,lum(source))
    out=[]
    for a,b in zip(source,back):
        if mode=='Normal':v=a
        elif mode=='Multiply':v=a*b
        elif mode=='Screen':v=a+b-a*b
        elif mode=='Overlay':v=2*a*b if b<=.5 else 1-2*(1-a)*(1-b)
        elif mode=='Darken':v=min(a,b)
        elif mode=='Lighten':v=max(a,b)
        elif mode=='ColorDodge':v=1 if a==1 else min(1,b/(1-a))
        elif mode=='ColorBurn':v=0 if a==0 else 1-min(1,(1-b)/a)
        elif mode=='HardLight':v=2*a*b if a<=.5 else 1-2*(1-a)*(1-b)
        elif mode=='SoftLight':v=b-(1-2*a)*b*(1-b) if a<=.5 else b+(2*a-1)*(((16*b-12)*b+4)*b if b<=.25 else math.sqrt(b))- (2*a-1)*b
        elif mode=='Difference':v=abs(a-b)
        else:v=a+b-2*a*b
        out.append(v)
    return out


def main():
    probe=str(Path(sys.argv[1]).resolve());count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-transparency-') as directory:
        def check(name,state,content,expected=None,extra=None,reject=False,reference=True,secondary=False):
            nonlocal count
            count+=1
            objects={8:'<< '+state+' >>'};objects.update(extra or {})
            xobjects=(' /Im 9 0 R' if 9 in objects else '')+(' /F 10 0 R' if 10 in objects else '')
            pdf=document('/MediaBox [0 0 18 12] /Resources << /ExtGState << /G 8 0 R /Opaque << /ca 1 /CA 1 /BM /Normal >> >> /XObject << '+xobjects+' >> >>',
                         stream=content,extra=objects)
            path=Path(directory)/(name+'.pdf');path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),(name,run.stderr)
            if reject:return
            assert struct.unpack('=III',run.stdout[:12])==(24,16,1)
            if expected:
                wanted=bytes(c for y in range(16)for x in range(24)for c in (*expected(x,y),255))
                assert run.stdout[12:]==wanted,(name,'analytic pixels differ',next((i,a,b)for i,(a,b)in enumerate(zip(run.stdout[12:],wanted))if a!=b))
            if '--reference' in sys.argv and reference:
                if secondary:
                    import fitz
                    with fitz.open(path) as reference_document:
                        pixmap=reference_document[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,rgb=pixmap.width,pixmap.height,pixmap.samples
                else:
                    prefix=Path(directory)/'reference' 
                    subprocess.run(['pdftoppm','-singlefile','-r','96','-aaVector','no',str(path),str(prefix)],check=True,capture_output=True,timeout=15)
                    w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(c for i,c in enumerate(run.stdout[12:])if i%4!=3)
                assert(w,h)==(24,16)
                assert max(abs(a-b)for a,b in zip(actual,rgb))<=1,(name,'reference differs')
        for mode in MODES:
            for alpha in (0,.25,1):
                for source,back in (((51,102,153),(204,51,102)),((255,0,255),(0,255,0)),((128,128,128),(0,0,0))):
                    normalized=[c/255 for c in source];backnorm=[c/255 for c in back]
                    mixed=blend(mode,normalized,backnorm)
                    wanted=tuple(int(max(0,min(1,alpha*c+(1-alpha)*b))*255+.5)for c,b in zip(mixed,backnorm))
                    content=' '.join(str(c/255)for c in back)+' rg 0 0 18 12 re f /G gs '+' '.join(str(c/255)for c in source)+' rg 0 0 18 12 re f'
                    check(f'{mode}-{alpha}-{count}',f'/ca {alpha} /BM /{mode}',content,lambda x,y:wanted)
        check('restore','/ca .5','q /G gs 1 0 0 rg 0 0 9 12 re f Q 0 0 1 rg 9 0 9 12 re f',lambda x,y:(255,128,128)if x<12 else(0,0,255))
        check('indirect-alpha','/ca 11 0 R','/G gs 1 0 0 rg 0 0 18 12 re f',lambda x,y:(255,128,128),extra={11:'.5'})
        green_red = '0 1 0 rg 0 0 18 12 re f /G gs 1 0 0 rg 0 0 18 12 re f'
        check('escaped-blend', '/BM /Mul#74iply', green_red, lambda x,y:(0,0,0))
        check('indirect-blend', '/BM 11 0 R', green_red, lambda x,y:(0,0,0), extra={11:'/Multiply'})
        check('indirect-blend-array', '/BM 11 0 R', green_red, lambda x,y:(0,0,0),
              extra={11:'[/Future 12 0 R /Screen]',12:'/Mul#74iply'})
        check('empty-name-fallback', '/BM [/ /Multiply]', green_red, lambda x,y:(0,0,0),reference=False)
        nulls = '/ca null /CA null /LW null /LC null /LJ null /ML null /D null /BM null /AIS null /TK null /SMask null /Font null'
        check('null-state', nulls, '/G gs 1 0 0 rg 0 0 18 12 re f', lambda x,y:(255,0,0))
        check('indirect-null-state', '/ca 11 0 R /BM 12 0 R',
              '/G gs 1 0 0 rg 0 0 18 12 re f',lambda x,y:(255,0,0),
              extra={11:'13 0 R',12:'null',13:'null'})
        page='<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 18 12] /Resources << /ExtGState << /G 8 0 R /H 11 0 R >> >> >>'
        check('null-preserves-state', '/ca .5 /BM /Multiply',
              '0 1 0 rg 0 0 18 12 re f /G gs /H gs 1 0 0 rg 0 0 18 12 re f',
              lambda x,y:(0,128,0),extra={3:page,11:'<< '+nulls+' >>'},reference=False)
        for state in ('/BM [/Normal /Mul#GG]', '/BM [/Normal /#00]', '/BM [null /Normal]'):
            check('invalid-blend-name-'+str(count),state,'/G gs',reject=True)
        check('cyclic-blend-reference','/BM 11 0 R','/G gs',extra={11:'12 0 R',12:'11 0 R'},reject=True)
        check('blend-array','/BM [/Future /Multiply /Screen]','0 1 0 rg 0 0 18 12 re f /G gs 1 0 0 rg 0 0 18 12 re f',lambda x,y:(0,0,0))
        check('blend-fallback','/BM [/Future]','/G gs 1 0 0 rg 0 0 18 12 re f',lambda x,y:(255,0,0))
        check('soft-mask-none','/SMask /None /ca .5','/G gs 1 0 0 rg 0 0 18 12 re f',lambda x,y:(255,128,128))
        # A duplicated segment belongs to one stroke, not two paint operations.
        check('stroke-once','/CA .5','/G gs 1 0 0 RG 3 w 0 6 m 18 6 l 0 6 m 18 6 l S',
              lambda x,y:(255,128,128)if 6<=y<10 else(255,255,255))
        # ISO 32000-1 11.6.2 forbids self-compositing a combined fill/stroke.
        # Use the secondary oracle, which treats it as one object.
        check('fill-stroke-once','/ca .5 /CA .5','/G gs 1 0 0 rg 0 0 1 RG 3 w 0 0 18 12 re B',
              lambda x,y:(128,128,255)if x<2 or x>=22 or y<2 or y>=14 else(255,128,128),secondary=True)
        mask=image_stream(b'\x80','/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray')
        img=image_stream(b'\xff\x00\x00','/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB /SMask 11 0 R')
        check('image-mask-times-alpha','/ca .5','/G gs 18 0 0 12 0 0 cm /Im Do',lambda x,y:(255,191,191),extra={9:img,11:mask})
        body=b'/Opaque gs 1 0 0 rg 0 0 9 12 re f'
        form=f'<< /Subtype /Form /BBox [0 0 18 12] /Length {len(body)} >>\nstream\n'.encode()+body+b'\nendstream'
        check('form-restores-alpha','/ca .5','/G gs /F Do 0 0 1 rg 9 0 9 12 re f',
              lambda x,y:(255,0,0)if x<12 else(128,128,255),extra={10:form})
        check('stroke-independent-alpha','/ca 0 /CA 1','/G gs 1 0 0 RG 3 w 0 6 m 18 6 l S',
              lambda x,y:(255,0,0)if 6<=y<10 else(255,255,255))
        check('blend-state-restored','/BM /Multiply','0 1 0 rg 0 0 18 12 re f q /G gs 1 0 0 rg 0 0 9 12 re f Q 1 0 0 rg 9 0 9 12 re f',
              lambda x,y:(0,0,0)if x<12 else(255,0,0))
        check('cyclic-alpha','/ca 11 0 R','/G gs',extra={11:'12 0 R',12:'11 0 R'},reject=True)
        check('state-line-width','/CA .5 /LW 3','/G gs 1 0 0 RG 0 6 m 18 6 l S',
              lambda x,y:(255,128,128)if 6<=y<10 else(255,255,255))
        check('compatible-blend','/BM /Compatible','/G gs 1 0 0 rg 0 0 18 12 re f',lambda x,y:(255,0,0))
        check('empty-blend-array','/BM []','/G gs 1 0 0 rg 0 0 18 12 re f',lambda x,y:(255,0,0))
        for state in ('/ca -1','/CA 2','/ca /Wrong','/BM 1','/BM [/Normal 1]','/SMask << /S /Alpha >>','/LW -1'):
            check('invalid-'+str(count),state,'/G gs 0 0 18 12 re f',reject=True)
    print(f'PDF transparency: {count} passed')


if __name__=='__main__':main()
