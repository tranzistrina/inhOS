#!/usr/bin/env python3
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'desktop'))
from inhfs.books import from_fb2, envelope
from inhfs.client import prepare

XML='''<?xml version="1.0" encoding="windows-1251"?><FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0"><body><title><p>Книга</p></title><section><title><p>Часть 1</p></title><p>Ёж и яма.</p><section><title><p>Глава <emphasis>первая</emphasis></p></title><p>Много текста. %s</p><poem><stanza><v>Строка стиха</v></stanza></poem></section><section><title><p>Глава вторая</p></title><p>Конец.</p></section></section></body><body name="notes"><section><title><p>Примечания</p></title><p>Сноска.</p></section></body></FictionBook>'''%('Слова '*1000)

def unpack(data):
    start,size,body_crc,table_crc=[int(x,16) for x in data[10:45].split()]
    assert len(data)==start+size
    body=data[start:];table=data[46:start]
    assert zlib.crc32(body)==body_crc and zlib.crc32(table)==table_crc
    entries=[]
    for line in table.splitlines():
        offset,depth,title=line.split(b'\t',2)
        entries.append((int(offset,16),int(depth,16),title.decode('utf-8')))
    return entries,body

class BookTests(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def native(self,data,ok=True):
        path=self.root/'book.txt';path.write_bytes(data)
        result=subprocess.run([str(ROOT/'build/book_check'),str(path)],capture_output=True,text=True)
        if ok:
            self.assertEqual(result.returncode,0,result.stderr);return json.loads(result.stdout)
        self.assertEqual(result.returncode,2,result.stderr)
    def test_encoding_nested_sections_and_native_navigation(self):
        raw=XML.encode('cp1251');data=from_fb2(raw);entries,body=unpack(data)
        self.assertEqual(len(entries),4);self.assertEqual([x[1] for x in entries],[0,1,1,0])
        self.assertEqual(entries[1][2],'Глава первая');self.assertIn('Ёж и яма.'.encode(),body)
        self.assertIn('Строка стиха'.encode(),body)
        for offset,depth,title in entries:self.assertTrue(body[offset:].decode().startswith(title))
        report=self.native(data);self.assertEqual(report['chapters'],4)
        source=self.root/'sample.fb2';source.write_bytes(raw)
        converted,remote=prepare(source,self.root);self.assertEqual(remote,'/book.txt');self.assertEqual(converted.read_bytes(),data)
        self.native(data) # reload/unchanged metadata
    def test_plain_txt_and_no_titles(self):
        self.assertEqual(self.native('Обычная книга\n'.encode())['chapters'],0)
        data=from_fb2(b'<FictionBook><body><section><p>Text</p></section></body></FictionBook>')
        self.assertEqual(self.native(data)['chapters'],0)
    def test_corruption_semantics_and_bounds(self):
        data=from_fb2(XML.encode('cp1251'))
        for at in (10,20,30,40,46,len(data)-1):
            bad=bytearray(data);bad[at]^=1;self.native(bad,False)
        self.native(data[:-1],False)
        body=b'abcdef\n'
        self.native(envelope(body,[(20,0,'Bad')]),False)
        self.native(envelope(body,[(4,0,'B'),(2,0,'A')]),False)
        with self.assertRaises(ValueError):envelope(body,[(0,0,'Chapter')]*1025)
        with self.assertRaises(ValueError):from_fb2(b'<!DOCTYPE a [<!ENTITY x "x">]><FictionBook/>')
        with self.assertRaises(Exception):from_fb2(b'<broken>')
        with self.assertRaises(ValueError):from_fb2(b'<FictionBook><body/></FictionBook>')
    def test_long_unicode_titles_and_large_toc(self):
        data=envelope(('я\n'*100).encode(),[(0,0,'Я'*200)])
        entries,body=unpack(data);self.assertLessEqual(len(entries[0][2].encode()),192);self.native(data)
        data=envelope(('Text\n'*10000).encode(),[(i*5,0,f'Chapter {i}') for i in range(1024)])
        self.assertEqual(self.native(data)['chapters'],1024)
    def test_python_portal_byte_parity(self):
        raw=XML.encode('cp1251');tree=ET.fromstring(raw)
        def node(e):return {'localName':e.tag.split('}')[-1],'nodeType':1,'textContent':''.join(e.itertext()),'childNodes':[node(x) for x in e]}
        fixture=self.root/'dom.json';fixture.write_text(json.dumps(node(tree)),encoding='utf-8')
        expected=self.root/'expected.bin';expected.write_bytes(from_fb2(raw))
        subprocess.run(['node',str(ROOT/'test/book_portal_test.cjs'),str(fixture),str(expected)],cwd=ROOT,check=True)

if __name__=='__main__':unittest.main(verbosity=2)
