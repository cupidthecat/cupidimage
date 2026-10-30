#!/usr/bin/env python3
"""Generate owned instruction glyphs; FontTools 4.61.1 reproduces the pinned bytes."""
import base64
import io
import json
import struct
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.ttLib import newTable
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphComponent
from fontTools.ttLib.tables.ttProgram import Program
def push(*values):
    if all(0 <= value <= 255 for value in values):
        return bytes([0xB0 + len(values) - 1, *values])
    return bytes([0xB8 + len(values) - 1]) + struct.pack('>' + 'h' * len(values), *values)


def build(point_size=False):
    pen=TTGlyphPen(None)
    pen.moveTo((31,35));pen.lineTo((631,35));pen.lineTo((631,835));pen.lineTo((31,835));pen.closePath()
    a=pen.glyph()
    a.program=Program()
    a.program.fromBytecode(b'\x01'+push(4)+b'\x17'+push(0,1,2,3,64)+b'\x38'+
                          push(5,512)+b'\x38\x00'+push(4)+b'\x17'+push(0,1,2,3,32)+b'\x38')
    b=Glyph();b.numberOfContours=-1
    component=GlyphComponent();component.glyphName='A';component.x=73;component.y=-31
    component.flags=0x1000|512
    component.transform=[[.75,.25],[-.125,1]]
    b.components=[component]
    b.program=Program();b.program.fromBytecode(b'\x01'+push(0)+b'\x2f')
    order=['.notdef','A','B']
    font=FontBuilder(1000,isTTF=True)
    font.setupGlyphOrder(order);font.setupCharacterMap({65:'A',66:'B'})
    font.setupGlyf({'.notdef':TTGlyphPen(None).glyph(),'A':a,'B':b})
    font.setupHorizontalMetrics({'.notdef':(500,0),'A':(700,31),'B':(900,0)})
    font.setupHorizontalHeader(ascent=1000,descent=-200)
    font.setupNameTable({'familyName':'CupidImage Hinted Shapes','styleName':'Regular',
        'uniqueFontIdentifier':'CupidImage Hinted Shapes','fullName':'CupidImage Hinted Shapes',
        'psName':'CupidImageHintedShapes'})
    font.setupOS2(sTypoAscender=1000,sTypoDescender=-200,usWinAscent=1000,usWinDescent=200)
    font.setupPost();font.setupMaxp()
    font.font['maxp'].maxStackElements=64
    font.font['maxp'].maxStorage=1
    if point_size:
        if point_size is True:
            a.program.fromBytecode(b'\x01'+push(4)+b'\x17'+push(0,1,2,3)+push(0)+b'\x43\x38')
            font.font['prep']=newTable('prep')
            font.font['prep'].program=Program()
            font.font['prep'].program.fromBytecode(push(0)+b'\x4c\x42')
        else:
            a.program.fromBytecode(b'\x01'+push(4)+b'\x17'+push(0,1,2,3,point_size)+b'\x38')
    font.font.recalcTimestamp=False
    font.font['head'].flags &= ~2
    font.font['head'].created=font.font['head'].modified=3400000000
    out=io.BytesIO();font.save(out)
    return out.getvalue()


if __name__=='__main__':
    data={'description':'Owned simple/composite glyphs with point shifts and a changed phantom advance; no third-party outlines.',
          'font':base64.b64encode(build()).decode(),
          'point_size_font':base64.b64encode(build(True)).decode(),
          'point_size_1158_font':base64.b64encode(build(1158)).decode(),
          'point_size_1165_font':base64.b64encode(build(1165)).decode()}
    path=Path(__file__).resolve().parents[1]/'tests/fixtures/pdf-page-hinting.json'
    path.write_text(json.dumps(data,indent=2)+'\n')
