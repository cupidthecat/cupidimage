#!/usr/bin/env python3
"""Offline tests for review selection, ratings, and resume fingerprints."""
from pathlib import Path
import argparse
import hashlib
import json
import tempfile
import unittest
from review_assets import ask, automatic_failure, collect, label, review_key, select_range
from pdf_render_options import OPTIONS, add_pdf_options, pdf_option_values, pdf_option_arguments, supports_pdf_options


class ReviewTests(unittest.TestCase):
    def test_ranges(self):
        self.assertEqual(select_range('1-5,3,8',10),[0,1,2,3,4,7])
        self.assertEqual(select_range('all',3),[0,1,2])
        self.assertEqual(select_range('first',3),[0])
        self.assertEqual(select_range('4-999999999',5),[3,4])
        for value in ('0','-1','3-1','bad','1-2-3',''):
            with self.subTest(value=value),self.assertRaises(ValueError):select_range(value,10)

    def test_selection(self):
        with tempfile.TemporaryDirectory()as temp:
            root=Path(temp);(root/'negative').mkdir()
            for name in ('a.png','b.PDF','c.jpg','notes.txt','negative/bad.png'):(root/name).write_bytes(b'x')
            rows=collect([str(root)],formats=['png,pdf'])
            self.assertEqual([Path(row['name']).name for row in rows],['a.png','b.PDF'])
            self.assertEqual(len(collect([str(root)],formats=['png'],include_negative=True)),2)
            self.assertEqual(len(collect([str(root/'a.png'),str(root/'b.PDF')])),2)
            self.assertEqual(len(collect([str(root)],formats=['jpg'])),1)
            self.assertEqual(len(collect([str(root)],pattern='a.*')),1)
            with self.assertRaises(ValueError):collect([str(root)],formats=['no-such-format'])
        self.assertEqual(len(collect([],pdfjs=True)),1007)

    def test_ratings(self):
        for answer,expected in [('c','correct'),('1','correct'),('p','partial'),('half bad','partial'),('w','wrong'),('s','skip'),('','skip'),('q','quit'),('r','redisplay')]:
            replies=iter([answer,'note'])
            rating,note=ask(lambda _:next(replies))
            self.assertEqual(rating,expected)
            if expected in ('partial','wrong'):self.assertEqual(note,'note')
        replies=iter(['n','text is missing','p'])
        self.assertEqual(ask(lambda _:next(replies)),('partial','text is missing'))

    def test_failed_render_is_automatically_wrong(self):
        for status in ('error','signal','timeout','invalid-output'):
            details={'decode_status':status,'error':'failed\n'}
            self.assertTrue(automatic_failure(details))
            self.assertEqual(details['rating'],'wrong')
            self.assertEqual(details['rating_source'],'automatic')
        self.assertFalse(automatic_failure({'decode_status':'ok'}))
        self.assertTrue(automatic_failure({'decode_status':'ok','terminal_status':'error'}))
        self.assertTrue(automatic_failure({'decode_status':'ok','artifact_error':'invalid raster'}))

    def test_pdf_options(self):
        parser=argparse.ArgumentParser()
        add_pdf_options(parser)
        values=pdf_option_values(parser.parse_args(['--pdf-text-aa','--pdf-cff-hinting']))
        self.assertEqual(values,{'text_antialias':True,'truetype_hinting':False,'cff_hinting':True})
        self.assertEqual(pdf_option_arguments(values),['--pdf-text-aa','--pdf-cff-hinting'])
        capabilities=json.dumps({'schema':1,'pdf_options':[flag for flag,_,_ in OPTIONS]})
        self.assertTrue(supports_pdf_options(capabilities,values))
        for bad in ('legacy probe', '{}', '[]', '{"schema":true,"pdf_options":[]}',
                    '{"schema":1,"pdf_options":[[]]}', '{"schema":1,"pdf_options":["--pdf-text-aa"]}'):
            self.assertFalse(supports_pdf_options(bad,values))
        self.assertTrue(supports_pdf_options('legacy probe',{}))
        base=review_key('/image','sha',0,'probe','cli','sixel')
        legacy=hashlib.sha256(json.dumps(['/image','sha',0,'probe','cli','sixel',False,'local reviewer']).encode()).hexdigest()
        self.assertEqual(base,legacy)
        disabled={field:False for _,field,_ in OPTIONS}
        self.assertEqual(base,review_key('/image','sha',0,'probe','cli','sixel',pdf_options=disabled))
        keys=set()
        for bits in range(1,16):
            enabled={field:bool(bits&(1<<i)) for i,(_,field,_) in enumerate(OPTIONS)}
            key=review_key('/image','sha',0,'probe','cli','sixel',pdf_options=enabled)
            self.assertNotEqual(base,key);keys.add(key)
            self.assertEqual(key,review_key('/image','sha',0,'probe','cli','sixel',pdf_options=dict(reversed(list(enabled.items())))))
        self.assertEqual(len(keys),15)

        legacy_profile=review_key('/image','sha',0,'probe','cli','sixel',pdf_options=values)
        self.assertEqual(legacy_profile,review_key('/image','sha',0,'probe','cli','sixel',pdf_options={**values,'type1_hinting':False}))
        type1=pdf_option_values(parser.parse_args(['--pdf-type1-hinting']))
        self.assertTrue(type1['type1_hinting'])
        old_caps=json.dumps({'schema':1,'pdf_options':[flag for flag,_,_ in OPTIONS[:-1]]})
        self.assertFalse(supports_pdf_options(old_caps,type1))

    def test_fingerprints(self):
        base=review_key('/image','sha',0,'probe','cli','auto')
        self.assertEqual(base,review_key('/image','sha',0,'probe','cli','auto'))
        for args in [('/image','changed',0,'probe','cli','auto'),('/image','sha',1,'probe','cli','auto'),('/image','sha',0,'new-probe','cli','auto'),('/image','sha',0,'probe','cli','ansi')]:
            self.assertNotEqual(base,review_key(*args))
        self.assertNotEqual(base,review_key('/image','sha',0,'probe','cli','auto',True))
        self.assertNotIn('\x1b',label('filename\x1b[2J'))


if __name__=='__main__':unittest.main()
