#!/usr/bin/env python3
"""Check binary font tables and exact decoded glyph points without font tools."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def tables(font):
    result={}
    for i in range(struct.unpack_from('>H',font,4)[0]):
        at=12+i*16;tag,checksum,offset,length=struct.unpack_from('>4sIII',font,at);result[tag]=(at,offset,length)
    return result


def replace_tables(font,changes):
    original=tables(font);tags=sorted(original);body=bytearray();records=[];offset=12+16*len(tags)
    for tag in tags:
        _,start,length=original[tag];data=changes.get(tag,font[start:start+length]);padded=data+b'\0'*((-len(data))%4)
        checksum=sum(struct.unpack('>'+str(len(padded)//4)+'I',padded))&0xffffffff if padded else 0
        records.append(struct.pack('>4sIII',tag,checksum,offset,len(data)));body.extend(padded);offset+=len(padded)
    return struct.pack('>IHHHH',0x10000,len(tags),0,0,0)+b''.join(records)+body


def main():
    probe=str(Path(sys.argv[1]).resolve());fixture=json.loads(Path('tests/fixtures/pdf-truetype.json').read_text());font=base64.b64decode(fixture['font']);count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-truetype-') as tmp:
        path=Path(tmp)/'font.ttf'
        def run(data,*args,status=0):
            nonlocal count
            path.write_bytes(data);result=subprocess.run([probe,str(path),*map(str,args)],capture_output=True,timeout=10)
            assert result.returncode==status,(args,status,result.returncode,result.stderr);count+=1;return result.stdout.decode()
        for glyph,outline in enumerate(fixture['outlines']):
            output=run(font,glyph).splitlines();assert list(map(int,output[0].split()))==[1000,len(fixture['outlines']),1000,len(outline['points'])]
            assert [list(map(float,line.split())) for line in output[1:]]==outline['points'],outline['name']
        for code,glyph in ((65,1),(66,2),(67,3),(68,4),(69,5),(0x10000,1),(0x10001,0),(0,0)):
            assert int(run(font,'map',3,10,code))==glyph
        for code,glyph in ((65,1),(69,5),(0xffff,0)):
            assert int(run(font,'map',3,1,code))==glyph
        for glyph,name in ((1,'box'),(2,'hole'),(4,'scaled'),(5,'attached'),(0,'unknown')):
            assert int(run(font,'post',name))==glyph
        encodings=json.loads(Path('tests/fixtures/pdf-encodings.json').read_text())
        for name in ('StandardEncoding','MacRomanEncoding','WinAnsiEncoding','MacExpertEncoding'):
            assert run(font,'encoding',name).splitlines()==encodings[name]
        source=Path('third_party/adobe-glyph-list/glyphlist.txt').read_text()
        names=[];want=[]
        for line in source.splitlines():
            if not line or line.startswith('#'):continue
            name,values=line.split(';');names.append(name);want.append(int(values,16) if ' ' not in values else 0)
        names+=['uni0041','u10000','A.alt','uniD800','u110000','A_B','uni00410042'];want +=[65,65536,65,0,0,0,0]
        path.write_bytes(font)
        result=subprocess.run([probe,str(path),'names'],input='\n'.join(names),capture_output=True,text=True,timeout=15)
        assert result.returncode==0 and list(map(int,result.stdout.split()))==want
        count+=len(names)
        subtables=[struct.pack('>HHH',0,262,0)+bytes(1 if c==65 else 2 if c==66 else 0 for c in range(256)),
                   struct.pack('>HHHHHHH',6,14,0,65,2,1,2),
                   struct.pack('>14H',4,36,0,4,4,1,0,66,65535,0,65,65535,0,1)+struct.pack('>4H',4,0,1,2),
                   struct.pack('>HHIII',12,0,28,0,1)+struct.pack('>III',65,66,1)]
        for table in subtables:
            cmap=struct.pack('>HHHHI',0,1,3,1,12)+table
            changed=replace_tables(font,{b'cmap':cmap})
            for code,glyph in ((64,0),(65,1),(66,2),(67,0)):
                assert int(run(changed,'map',3,1,code))==glyph
        constant=struct.pack('>HHIII',13,0,28,0,1)+struct.pack('>III',65,66,2)
        changed=replace_tables(font,{b'cmap':struct.pack('>HHHHI',0,1,3,10,12)+constant})
        assert int(run(changed,'map',3,10,65))==2 and int(run(changed,'map',3,10,66))==2
        mapping=tables(font);_,lo,n=mapping[b'loca'];locations=struct.unpack('>'+str(n//2)+'H',font[lo:lo+n]);_,head_at,head_length=mapping[b'head'];head=bytearray(font[head_at:head_at+head_length]);struct.pack_into('>H',head,50,1)
        changed=replace_tables(font,{b'loca':struct.pack('>'+str(len(locations))+'I',*(x*2 for x in locations)),b'head':bytes(head)})
        for glyph,outline in enumerate(fixture['outlines']):
            output=run(changed,glyph).splitlines();assert [list(map(float,line.split())) for line in output[1:]]==outline['points']
        run(font,len(fixture['outlines']),status=3)
        for length in (0,4,11,20,len(font)//2):run(font[:length],0,status=2)
        mapping=tables(font)
        for tag,relative,value,fmt in ((b'head',18,0,'H'),(b'head',18,16385,'H'),(b'head',50,2,'H'),(b'head',12,0,'I'),(b'maxp',4,0,'H'),(b'hhea',34,0,'H')):
            broken=bytearray(font);struct.pack_into('>'+fmt,broken,mapping[tag][1]+relative,value);run(broken,1,status=2)
        broken=bytearray(font);struct.pack_into('>I',broken,mapping[b'glyf'][0]+12,len(font));run(broken,1,status=2)
        broken=bytearray(font);struct.pack_into('>H',broken,mapping[b'loca'][1]+2,65535);run(broken,1,status=2)
        _,loca,_=mapping[b'loca'];_,glyf,_=mapping[b'glyf']
        start=glyf+struct.unpack_from('>H',font,loca+4*2)[0]*2
        legacy=bytearray(font);struct.pack_into('>H',legacy,start+10,struct.unpack_from('>H',font,start+10)[0]|0x10)
        assert run(legacy,4)==run(font,4)
        broken=bytearray(font);struct.pack_into('>H',broken,start+12,4);run(broken,4,status=3)
        start=glyf+struct.unpack_from('>H',font,loca+1*2)[0]*2
        broken=bytearray(font);struct.pack_into('>H',broken,start+12,65535);run(broken,1,status=3)
    print(f'TrueType tables/outlines: {count} checks passed')
if __name__=='__main__':main()
