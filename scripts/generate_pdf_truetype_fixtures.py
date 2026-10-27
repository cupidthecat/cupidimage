#!/usr/bin/env python3
"""Regenerate authored font fixtures. Requires fontTools; regular tests do not."""
import base64
import io
import json
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphComponent, GlyphCoordinates


def polygon(contours):
    pen=TTGlyphPen(None)
    for points in contours:
        pen.moveTo(points[0])
        for point in points[1:]:pen.lineTo(point)
        pen.closePath()
    return pen.glyph()


def main():
    names=['.notdef','box','hole','quadratic','scaled','attached','offstart','offends']
    glyphs={'.notdef':polygon([]),
            'box':polygon([[(0,0),(0,600),(400,600),(400,0)]]),
            'hole':polygon([[(0,0),(0,600),(600,600),(600,0)],[(150,150),(450,150),(450,450),(150,450)]])}
    pen=TTGlyphPen(None);pen.moveTo((0,0));pen.qCurveTo((0,600),(300,800),(600,600),(600,0));pen.closePath();glyphs['quadratic']=pen.glyph()
    pen=TTGlyphPen(glyphs);pen.addComponent('box',(1,0,0,1,0,0));pen.addComponent('box',(.5,0,.25,.75,500,0));glyphs['scaled']=pen.glyph()
    component=GlyphComponent();component.glyphName='box';component.x=0;component.y=0;component.flags=0
    attached=GlyphComponent();attached.glyphName='box';attached.firstPt=2;attached.secondPt=0;attached.flags=0
    glyph=Glyph();glyph.numberOfContours=-1;glyph.components=[component,attached];glyphs['attached']=glyph
    import copy
    coords,ends,flags=glyphs['quadratic'].getCoordinates(None)
    for name,shift in (('offstart',1),('offends',2)):
        g=copy.deepcopy(glyphs['quadratic']);g.coordinates=GlyphCoordinates(list(coords[shift:])+list(coords[:shift]));g.flags=flags[shift:]+flags[:shift];glyphs[name]=g
    builder=FontBuilder(1000,isTTF=True);builder.setupGlyphOrder(names);builder.setupCharacterMap({65:'box',66:'hole',67:'quadratic',68:'scaled',69:'attached',70:'offstart',71:'offends',32:'box',0x20ac:'box',0xa4:'hole',0x10000:'box'})
    builder.setupGlyf(glyphs);builder.setupHorizontalMetrics({name:(1000,0) for name in names});builder.setupHorizontalHeader(ascent=1000,descent=-200)
    builder.setupNameTable({'familyName':'CupidImage Test Shapes','styleName':'Regular','uniqueFontIdentifier':'CupidImageTestShapes','fullName':'CupidImage Test Shapes','psName':'CupidImageTestShapes'})
    builder.setupOS2(sTypoAscender=1000,sTypoDescender=-200,usWinAscent=1400,usWinDescent=200)
    builder.setupPost();builder.setupMaxp();builder.font['head'].created=builder.font['head'].modified=3406620153
    builder.font.recalcTimestamp=False
    output=io.BytesIO();builder.save(output)
    outlines=[]
    for name in names:
        coords,ends,flags=builder.font['glyf'][name].getCoordinates(builder.font['glyf'])
        outlines.append({'name':name,'points':[[float(x),float(y),int(flag&1),int(i in ends)] for i,((x,y),flag) in enumerate(zip(coords,flags))]})
    payload={'description':'Original geometric glyphs authored for CupidImage tests; not a third-party typeface.','font':base64.b64encode(output.getvalue()).decode(),'outlines':outlines}
    raw=output.getvalue()
    Path('tests/fixtures/pdf-truetype.inc').write_text('/* Authored geometric test font; regenerate with generate_pdf_truetype_fixtures.py. */\nstatic const unsigned char test_truetype_font[]={\n'+''.join('    '+','.join(str(b) for b in raw[i:i+24])+',\n' for i in range(0,len(raw),24))+'};\n')
    Path('tests/fixtures/pdf-truetype.json').write_text(json.dumps(payload,indent=2)+'\n')
if __name__=='__main__':main()
