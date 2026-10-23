#!/usr/bin/env python3
"""Regenerate owned VP8 fixtures; requires Pillow (WebP) and FFmpeg (libvpx)."""
import PIL
from PIL import Image,features
from pathlib import Path
import argparse,base64,hashlib,json,random,subprocess,tempfile,zlib
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'tests/fixtures/vp8-reference.json')
parser.add_argument('--assets',type=Path,default=Path(__file__).resolve().parents[1]/'assets/webp/reference')
args=parser.parse_args()
args.assets.mkdir(parents=True,exist_ok=True)
workspace=tempfile.TemporaryDirectory(prefix='cupidimage-vp8-generate-')
root=Path(workspace.name)
rows=[]
def reference(name,path,w,h):
 encoded=path.read_bytes();raw=subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-f','rawvideo','-pix_fmt','yuv420p','pipe:1'])
 n=w*h;uw=(w+1)//2;uh=(h+1)//2;u=uw*uh;expected=bytearray()
 for y in range(h):
  for x in range(w):
   Y=raw[y*w+x];U=raw[n+(y//2)*uw+x//2]-128;V=raw[n+u+(y//2)*uw+x//2]-128;scaled=298*(Y-16)
   expected.extend([max(0,min(255,v)) for v in [(scaled+409*V+128)>>8,(scaled-100*U-208*V+128)>>8,(scaled+516*U+128)>>8]]+[255])
 asset=args.assets/(name+'.webp');asset.write_bytes(encoded)
 rows.append(dict(name=name,width=w,height=h,asset='assets/webp/reference/'+asset.name,sha256=hashlib.sha256(encoded).hexdigest(),rgba_zlib=base64.b64encode(zlib.compress(expected)).decode()))
for w,h in [(1,1),(4,3),(16,16),(17,19),(32,32)]:
 for kind in ['solid','gradient','noise']:
  rng=random.Random(6386+w*100+h)
  im=Image.new('RGB',(w,h));im.putdata([(40,80,120) if kind=='solid' else ((x*13+y*7)%256,(x*3+y*9)%256,(x*7+y*5)%256) if kind=='gradient' else tuple(rng.randrange(256)for _ in range(3)) for y in range(h)for x in range(w)])
  for q in [10,50,90]:
   name=f'{kind}-{w}x{h}-q{q}';path=root/(name+'.webp');im.save(path,format='WEBP',quality=q,method=6);reference(name,path,w,h)
w,h=64,66;im=Image.new('RGB',(w,h));im.putdata([((x*13+y*7)%256,(x*3+y*9)%256,(x*7+y*5)%256)for y in range(h)for x in range(w)]);png=root/'partitions.png';im.save(png)
for parts in [1,2,4,8]:
 ivf=root/f'partitions-{parts}.ivf';subprocess.run(['ffmpeg','-v','error','-i',str(png),'-frames:v','1','-c:v','libvpx','-deadline','best','-slices',str(parts),'-pix_fmt','yuv420p','-f','ivf',str(ivf),'-y'],check=True)
 d=ivf.read_bytes();n=int.from_bytes(d[32:36],'little');vp=d[44:44+n];chunk=b'VP8 '+len(vp).to_bytes(4,'little')+vp+(b'\0' if len(vp)%2 else b'');out=b'RIFF'+(len(chunk)+4).to_bytes(4,'little')+b'WEBP'+chunk;path=root/f'partitions-{parts}.webp';path.write_bytes(out);reference(f'partitions-{parts}',path,w,h)
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(dict(provenance={'source':'Owned synthetic constant, gradient, and deterministic random pixels; no external image content.','encoder':f'Pillow {PIL.__version__} / libwebp {features.version("webp")}; FFmpeg libvpx for 1/2/4/8 coefficient partitions','oracle':'FFmpeg VP8 decoder, YUV420 planes. Expected RGBA uses nearest-neighbor chroma and integer limited-range BT.601 conversion, not libwebp fancy upsampling.','seed':'6386 + width * 100 + height','quality':[10,50,90]},cases=rows),indent=2)+'\n')
print('saved',len(rows))

workspace.cleanup()
