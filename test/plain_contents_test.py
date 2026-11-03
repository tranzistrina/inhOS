#!/usr/bin/env python3
from pathlib import Path
import json
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class PlainTests(unittest.TestCase):
    def detect(self,text):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'book.txt';raw=text if isinstance(text,bytes) else text.encode('utf-8');path.write_bytes(raw)
            r=subprocess.run([str(ROOT/'build/plain_contents_check'),str(path)],capture_output=True,text=True,check=True)
            self.assertEqual(path.read_bytes(),raw)
            return json.loads(r.stdout)
    def test_old_flattened_fb2_and_offsets(self):
        text='Название книги\nЧасть 1. Созревание\n1.01\n'+('Это текст. '*300)+'\n1.02\nПродолжение.\nИнтерлюдия 1 (Денни)\nТекст\n'
        result=self.detect(text)
        self.assertEqual([e['title'] for e in result['entries']],['Часть 1. Созревание','1.01','1.02','Интерлюдия 1 (Денни)'])
        for entry in result['entries']:
            self.assertTrue(text.encode()[entry['offset']:].startswith(entry['title'].encode()))
        self.assertEqual([e['depth'] for e in result['entries']],[0,1,1,1])
    def test_normal_txt_case_crlf_bom(self):
        text='\ufeff   ГЛАВА ПЕРВАЯ\r\nПервый текст\r\n\tГлава II. Продолжение\r\nВторой текст\r\nЭПИЛОГ\r\nКонец'
        self.assertEqual([e['title'] for e in self.detect(text)['entries']],['ГЛАВА ПЕРВАЯ','Глава II. Продолжение','ЭПИЛОГ'])
    def test_numeric_and_roman_headings(self):
        for labels in [('I','II','III'),('1','2','3'),('1.2.1','1.2.2')]:
            text='\n'.join(label+'\nДлинный обычный абзац.' for label in labels)
            self.assertEqual([e['title'] for e in self.detect(text)['entries']],list(labels))
    def test_rejects_prose_dates_and_isolated_numbers(self):
        result=self.detect('Часть людей осталась.\nГлава города выступил.\n2026\n192.168.0.1\n3.14\n42\nI\nIIII\ncivil\nОбычный текст.\n')
        self.assertEqual(result['count'],0)
        self.assertEqual(self.detect('Глава I. Одинокая глава\nТекст')['count'],1)
    def test_large_file_limits_and_invalid_utf8(self):
        self.assertEqual(self.detect(b'\xff\n'+b'a'*10000+b'\n')['count'],0)
        text='\n'.join(f'Глава {i}\nТекст.' for i in range(1,1100))
        result=self.detect(text);self.assertEqual(result['count'],1024);self.assertTrue(result['truncated'])
        self.assertEqual(self.detect(('Абзац текста. '*250000)+'\nЭпилог\n')['count'],1)

if __name__=='__main__':unittest.main(verbosity=2)
