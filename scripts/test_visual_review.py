#!/usr/bin/env python3
"""Offline tests for review selection, ratings, and resume fingerprints."""
from pathlib import Path
import tempfile
import unittest
from review_assets import ask, collect, label, review_key, select_range


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

    def test_fingerprints(self):
        base=review_key('/image','sha',0,'probe','cli','auto')
        self.assertEqual(base,review_key('/image','sha',0,'probe','cli','auto'))
        for args in [('/image','changed',0,'probe','cli','auto'),('/image','sha',1,'probe','cli','auto'),('/image','sha',0,'new-probe','cli','auto'),('/image','sha',0,'probe','cli','ansi')]:
            self.assertNotEqual(base,review_key(*args))
        self.assertNotEqual(base,review_key('/image','sha',0,'probe','cli','auto',True))
        self.assertNotIn('\x1b',label('filename\x1b[2J'))


if __name__=='__main__':unittest.main()
