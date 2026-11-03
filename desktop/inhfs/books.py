"""FB2 -> validated UTF-8 text + native inhOS TOC envelope (INHBOOK/1)."""
from pathlib import Path
import re
import zlib
import xml.etree.ElementTree as ET
MAGIC=b'INHBOOK/1\n'
HEADER=46
MAX_CHAPTERS=1024
MAX_TABLE=65536


def title_bytes(text):
    text=' '.join(text.split())
    text=''.join(c for c in text if ord(c)>=32 and ord(c)!=127)
    raw=text.encode('utf-8')[:192]
    return raw.decode('utf-8',errors='ignore').encode('utf-8')


def envelope(body, entries):
    if not body or b'\0' in body:
        raise ValueError('В FB2 отсутствует текст или встречается NUL')
    if len(entries)>MAX_CHAPTERS:
        raise ValueError('В книге более 1024 разделов')
    table=bytearray()
    for offset,depth,title in entries:
        title=title_bytes(title)
        if title:
            table.extend(f'{offset:08x}\t{min(depth,15):02x}\t'.encode('ascii')+title+b'\n')
    if len(table)>MAX_TABLE:
        raise ValueError('Оглавление превышает 64 КиБ')
    start=HEADER+len(table)
    header=MAGIC+f'{start:08x} {len(body):08x} {zlib.crc32(body):08x} {zlib.crc32(table):08x}\n'.encode('ascii')
    assert len(header)==HEADER
    result=header+table+body
    if len(result)>3*1024*1024:
        raise ValueError('Книга с оглавлением превышает 3 МиБ')
    return result


def from_fb2(raw):
    if len(raw)>16*1024*1024:
        raise ValueError('FB2 превышает 16 МиБ')
    # ElementTree honors the XML encoding declaration (including Windows-1251).
    check=raw.replace(b'\0',b'').upper()
    if b'<!DOCTYPE' in check or b'<!ENTITY' in check:
        raise ValueError('DTD и сущности в FB2 запрещены')
    root=ET.fromstring(raw)
    local=lambda tag:tag.split('}')[-1]
    if local(root.tag)!='FictionBook':
        raise ValueError('Это не FictionBook/FB2')
    bodies=[e for e in root if local(e.tag)=='body']
    if not bodies:
        raise ValueError('В FB2 отсутствует текст книги')
    body=bytearray();entries=[]
    def line(text):
        body.extend(text.replace('\r\n','\n').replace('\r','\n').encode('utf-8'))
        body.extend(b'\n')
    # Iterative traversal avoids recursion failure in deeply nested XML.
    for main in bodies:
        stack=[(main,0)]
        while stack:
            element,depth=stack.pop()
            tag=local(element.tag)
            if tag=='section':
                title=next((x for x in element if local(x.tag)=='title'),None)
                label=' '.join(''.join(title.itertext()).split()) if title is not None else ''
                if label:
                    entries.append((len(body),min(depth,15),label))
                for child in reversed(list(element)):
                    stack.append((child,depth+1 if local(child.tag)=='section' else depth))
            elif tag in ('p','v','subtitle'):
                line(''.join(element.itertext()))
            elif tag=='empty-line':
                line('')
            elif tag not in ('binary','description','image'):
                for child in reversed(list(element)):
                    stack.append((child,depth))
    return envelope(bytes(body),entries)


def convert(source,destination):
    source=Path(source)
    if source.stat().st_size>16*1024*1024:
        raise ValueError('FB2 превышает 16 МиБ')
    Path(destination).write_bytes(from_fb2(source.read_bytes()))
