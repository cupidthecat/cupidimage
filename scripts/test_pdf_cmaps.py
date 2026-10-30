#!/usr/bin/env python3
"""Check encoding CMap decoding, inheritance and rendered glyph selection."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_embedded_fonts import fixture
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm


def cmap_pdf(content,body,attributes='',extra=None,compressed=False,**kwargs):
    program=body.encode() if isinstance(body,str) else body
    if compressed:program=zlib.compress(program);attributes+=' /Filter /FlateDecode'
    objects={5:b'<< /Type /Font /Subtype /Type0 /BaseFont /CupidImageTestShapes /Encoding 10 0 R /DescendantFonts [6 0 R] >>',10:stream(program,'/Type /CMap '+attributes)}
    objects.update(extra or {})
    return fixture(content,cid=True,extra=objects,**kwargs)


def main():
    probe=str(Path(sys.argv[1]).resolve());decoder=str(Path(sys.argv[2]).resolve());count=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-cmaps-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,body,encoded,expected,mode=0,reject=False,work=None,**kwargs):
            nonlocal count
            path.write_bytes(cmap_pdf('q Q',body,**kwargs));args=[decoder,str(path),encoded]+([] if work is None else [str(work)])
            run=subprocess.run(args,capture_output=True,text=True,timeout=15)
            assert run.returncode==(2 if reject else 3 if work==0 else 0),(name,run.returncode,run.stderr,run.stdout)
            count+=1
            if reject or work==0:return
            lines=run.stdout.splitlines();assert lines[0]=='mode '+str(mode),(name,lines)
            actual=[tuple(map(int,line.split())) for line in lines[1:]];assert actual==expected,(name,actual,expected)
        def same(name,body,encoded,identity,attributes='',extra=None,vertical=False,reference=True):
            nonlocal count,refs
            position='60 70' if vertical else '20 30'
            content=f'BT /F 30 Tf {position} Td <{encoded}> Tj ET'
            pdf=cmap_pdf(content,body,attributes,extra)
            path.write_bytes(pdf);actual=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            path.write_bytes(fixture(f'BT /F 30 Tf {position} Td <{identity}> Tj ET',cid=True,vertical=vertical));expected=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert actual.returncode==expected.returncode==0,(name,actual.stderr,expected.stderr)
            assert actual.stdout==expected.stdout,(name,'mapped glyphs differ')
            count+=1
            if '--reference' in sys.argv and reference:
                path.write_bytes(pdf);prefix=Path(tmp)/'reference';run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                assert run.returncode==0,run.stderr;w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                pixels=bytes(v for i,v in enumerate(actual.stdout[12:]) if i%4!=3)
                assert (w,h)==(160,120) and pixels==rgb,(name,'independent raster differs')
                refs+=1
        def content_same(name,body,content,expected,vertical=False,attributes=''):
            nonlocal count
            path.write_bytes(cmap_pdf(content,body,attributes));a=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            path.write_bytes(fixture(expected,cid=True,vertical=vertical));b=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert a.returncode==b.returncode==0 and a.stdout==b.stdout,(name,a.stderr,b.stderr)
            count+=1
        one='1 begincodespacerange <00> <FF> endcodespacerange '
        two='1 begincodespacerange <0000> <FFFF> endcodespacerange '
        mixed='3 begincodespacerange <00> <7F> <8140> <9FFC> <A0000000> <A0FFFFFF> endcodespacerange '
        mapping='2 begincidchar <41> 1 <42> 2 endcidchar '
        check('single-byte',one+mapping,'4142',[(1,65,1,0,1),(1,66,2,0,1)])
        check('compressed',one+mapping,'41',[(1,65,1,0,1)],compressed=True)
        check('range',one+'1 begincidrange <41> <42> 1 endcidrange','4142',[(1,65,1,0,1),(1,66,2,0,1)])
        check('mixed-lengths',mixed+'3 begincidchar <41> 1 <8140> 2 <A0000001> 4 endcidchar','418140A0000001',[(1,65,1,0,1),(2,0x8140,2,0,1),(4,0xA0000001,4,0,1)])
        check('source-range-bytewise',mixed,'820041',[(2,0x8200,0,0,0),(1,65,0,0,1)])
        check('unmatched-shortest',mixed,'FF41',[(1,255,0,0,0),(1,65,0,0,1)])
        check('partial-four-byte',mixed,'A000',[(2,0xA000,0,0,0)])
        check('notdef-constant',one+'1 beginnotdefrange <00> <1F> 5 endnotdefrange','0001',[(1,0,5,5,1),(1,1,5,5,1)])
        check('normal-before-notdef',one+mapping+'1 beginnotdefchar <41> 2 endnotdefchar','41',[(1,65,1,2,1)])
        check('last-definition',one+'1 begincidrange <40> <43> 1 endcidrange 1 begincidchar <41> 5 endcidchar','40414243',[(1,64,1,0,1),(1,65,5,0,1),(1,66,3,0,1),(1,67,4,0,1)])
        check('vertical',one+'/WMode 1 def '+mapping,'41',[(1,65,1,0,1)],mode=1,attributes='/WMode 1')
        check('identity-inheritance','/Identity-H usecmap 1 begincidchar <0041> 2 endcidchar','00410042',[(2,65,2,0,1),(2,66,66,0,1)],attributes='/UseCMap /Identity-H')
        base=stream(one+mapping,'/Type /CMap /CMapName /Base')
        check('stream-inheritance','/Base usecmap 1 begincidchar <42> 4 endcidchar','4142',[(1,65,1,0,1),(1,66,4,0,1)],attributes='/UseCMap 11 0 R',extra={11:base})
        check('comments-and-strings','(9 begincidchar <41> 9 endcidchar) pop % ignored\n'+one+mapping,'41',[(1,65,1,0,1)])
        check('empty-map',one,'41',[(1,65,0,0,1)])
        check('work-budget',one+mapping,'41',[],work=0)
        realistic='/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo 3 dict dup begin /Registry (Adobe) def /Ordering (Identity) def /Supplement 0 def end def /CMapName /Authored def /CMapType 1 def /WMode 0 def '+one+mapping+'endcmap CMapName currentdict /CMap defineresource pop end end'
        check('resource-syntax',realistic,'4142',[(1,65,1,0,1),(1,66,2,0,1)])
        check('hex-whitespace',one+'1 begincidchar < 4 1 > 1 endcidchar','41',[(1,65,1,0,1)])
        check('odd-hex-padding',one+'1 begincidchar <A> 1 endcidchar','A0',[(1,160,1,0,1)])
        check('four-byte-max','1 begincodespacerange <00000000> <FFFFFFFF> endcodespacerange 1 begincidchar <FFFFFFFF> 2 endcidchar','FFFFFFFF',[(4,0xffffffff,2,0,1)])
        vertical_base=stream('/WMode 1 def '+one+mapping,'/CMapName /BaseV /WMode 1')
        check('base-does-not-set-writing-mode','/BaseV usecmap','41',[(1,65,1,0,1)],attributes='/UseCMap 11 0 R',extra={11:vertical_base})
        check('explicit-writing-mode','/BaseV usecmap /WMode 1 def','41',[(1,65,1,0,1)],mode=1,attributes='/UseCMap 11 0 R /WMode 1',extra={11:vertical_base})
        many='1000 begincidchar '+''.join(f'<{code:04X}> {code%8} ' for code in range(1000))+'endcidchar'
        check('indexed-large-map',two+many,'03E7000001F4',[(2,999,7,0,1),(2,0,0,0,1),(2,500,4,0,1)])
        check('overlapping-late-range',one+mapping+'1 begincidrange <40> <43> 4 endcidrange','4142',[(1,65,5,0,1),(1,66,6,0,1)])
        check('duplicate-identical-codespace',one+one+mapping,'41',[(1,65,1,0,1)])
        check('large-notdef-domain','1 begincodespacerange <00000000> <FFFFFFFF> endcodespacerange 1 beginnotdefrange <00000000> <FFFFFFFF> 2 endnotdefrange','FFFFFFFF',[(4,0xffffffff,2,2,1)])
        check('truncated-code',two,'00',[(1,0,0,0,0)])
        check('three-byte-range','1 begincodespacerange <810000> <81FFFF> endcodespacerange 1 begincidrange <810000> <8101FF> 1 endcidrange','8100FE8100FF810100810101',[(3,0x8100fe,255,0,1),(3,0x8100ff,256,0,1),(3,0x810100,257,0,1),(3,0x810101,258,0,1)])
        partial='3 begincodespacerange <81400000> <8140FFFF> <814100> <8141FF> <90> <9F> endcodespacerange '
        check('partial-range-tie',partial,'814290',[(3,0x814290,0,0,0)])
        check('longer-partial-before-shortest',partial,'814080',[(3,0x814080,0,0,0)])
        check('shortest-no-prefix',partial,'FF90',[(1,255,0,0,0),(1,144,0,0,1)])
        notdef_base=stream(one+'1 beginnotdefrange <00> <FF> 2 endnotdefrange','/CMapName /Fallback')
        check('local-notdef-overrides-base','/Fallback usecmap 1 beginnotdefchar <41> 4 endnotdefchar','4142',[(1,65,4,4,1),(1,66,2,2,1)],attributes='/UseCMap 11 0 R',extra={11:notdef_base})
        check('inherited-normal-before-local-notdef','/Base usecmap 1 beginnotdefchar <41> 4 endnotdefchar','41',[(1,65,1,4,1)],attributes='/UseCMap 11 0 R',extra={11:base})
        check('lexical-data-not-operators','[ (begincidchar) /WMode 1 ] pop { 1 beginbfchar <00> <0041> endbfchar } pop '+one+mapping,'41',[(1,65,1,0,1)])
        for body in ('0 begincodespacerange endcodespacerange','1 begincodespacerange <81F0> <9F40> endcodespacerange',one+'1 begincidchar <0000000001> 1 endcidchar',one+'1 begincidchar <41> -1 endcidchar',one+'1 begincidchar <41> 1.5 endcidchar'):
            check('invalid-boundary',body,'41',[],reject=True)
        for invalid in ('1 begincodespacerange <00> <FFFF> endcodespacerange',
                        '2 begincodespacerange <00> <FF> <0040> <00FF> endcodespacerange',
                        one+'1 begincidchar <41> 65536 endcidchar',one+'2 begincidchar <41> 1 endcidchar',
                        one+'1 begincidrange <41> <42> 65535 endcidrange',one+'1 beginbfchar <41> <0041> endbfchar',
                        one+'1 usefont',one+'/WMode 2 def',one+'/Missing usecmap',
                        two+'1 begincidrange <0140> <02FC> 1 endcidrange'):
            check('invalid',invalid,'41',[],reject=True)
        check('named-map-inheritance','/90ms-RKSJ-H usecmap 1 begincidchar <8140> 2 endcidchar','418140',[(1,65,264,0,1),(2,0x8140,2,0,1)],attributes='/UseCMap /90ms-RKSJ-H')
        check('unknown-predefined',one,'41',[],reject=True,attributes='/UseCMap /Unknown-CMap')
        check('cycle',one,'41',[],reject=True,attributes='/UseCMap 10 0 R')
        check('mode-disagreement',one+'/WMode 1 def','41',[],reject=True,attributes='/WMode 0')
        same('render-cidchar',one+mapping,'4142','00010002')
        same('render-cidrange',one+'1 begincidrange <41> <42> 1 endcidrange','4142','00010002')
        same('render-mixed',mixed+'3 begincidchar <41> 1 <8140> 2 <A0000001> 1 endcidchar','418140A0000001','000100020001')
        same('render-inherited','/Base usecmap 1 begincidchar <42> 1 endcidchar','4142','00010001',attributes='/UseCMap 11 0 R',extra={11:base})
        same('notdef-missing-glyph',one+'1 begincidchar <41> 600 endcidchar 1 beginnotdefchar <41> 2 endnotdefchar','41','0002',reference=False)
        same('CID-zero-is-present',one+'1 begincidchar <41> 0 endcidchar 1 beginnotdefchar <41> 2 endnotdefchar','41','0000')
        same('notdef-unmapped-code',one+'1 beginnotdefchar <41> 2 endnotdefchar','41','0002',reference=False)
        same('normal-before-fallback',one+mapping+'1 beginnotdefchar <41> 2 endnotdefchar','41','0001')
        same('base-writing-default','/BaseV usecmap','4142','00010002',attributes='/UseCMap 11 0 R',extra={11:vertical_base})
        spaced=one+'2 begincidchar <20> 1 <41> 2 endcidchar'
        content_same('single-byte-word-spacing',spaced,'BT /F 30 Tf 5 Tw 20 30 Td <2041> Tj ET',
                     'BT /F 30 Tf 20 30 Td <0001> Tj 1 0 0 1 55 30 Tm <0002> Tj ET')
        content_same('two-byte-no-word-spacing',two+'2 begincidchar <0020> 1 <0041> 2 endcidchar','BT /F 30 Tf 5 Tw 20 30 Td <00200041> Tj ET',
                     'BT /F 30 Tf 20 30 Td <00010002> Tj ET')
        content_same('vertical-word-spacing',spaced+' /WMode 1 def','BT /F 30 Tf 5 Tw 60 70 Td <2041> Tj ET',
                     'BT /F 30 Tf 60 70 Td <0001> Tj 1 0 0 1 60 45 Tm <0002> Tj ET',vertical=True,attributes='/WMode 1')
        content_same('scaled-spaced-code',spaced,'BT /F 30 Tf 50 Tz 4 Tc 20 30 Td <2041> Tj ET',
                     'BT /F 30 Tf 50 Tz 20 30 Td <0001> Tj 1 0 0 1 37 30 Tm <0002> Tj ET')
        from test_pdf_cid import fixture as fallback_fixture
        raw=fallback_fixture('BT /CID 12 Tf 20 40 Td <41> Tj ET',extra={5:b'<< /Subtype /Type0 /Encoding 10 0 R /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>',7:stream('2 beginbfchar <41> <0041> <02> <0042> endbfchar'),10:stream(one+'1 begincidchar <41> 2 endcidchar')})
        path.write_bytes(raw);a=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
        path.write_bytes(fallback_fixture('BT /CID 12 Tf 20 40 Td <0001> Tj ET'));b=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
        assert a.returncode==b.returncode==0 and a.stdout==b.stdout,'ToUnicode must use original source code, not CID'
        count+=1
        same('vertical-render',one+'/WMode 1 def '+mapping,'4142','00010002',attributes='/WMode 1',vertical=True,reference=False)
    print(f'PDF encoding CMaps: {count} checks passed; {refs} independent comparisons')
if __name__=='__main__':main()
