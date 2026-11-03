#!/usr/bin/env python3
"""PNG/JPG to GINH v1; Pillow required. Fit inside 250x122, white letterbox."""
import argparse,struct,zlib
from pathlib import Path
HEADER=struct.Struct('<4sBBHHHII')
def encode(pixels):
    if len(pixels)!=3813 or pixels[-1]&15:raise ValueError('3813 bytes and zero padding nibble required')
    return HEADER.pack(b'GINH',1,20,250,122,0,3813,zlib.crc32(pixels))+pixels
def decode(data):
    if len(data)!=3833:raise ValueError('wrong file length')
    magic,version,header,w,h,flags,size,crc=HEADER.unpack(data[:20])
    p=data[20:]
    if (magic,version,header,w,h,flags,size)!=(b'GINH',1,20,250,122,0,3813) or p[-1]&15 or zlib.crc32(p)!=crc:raise ValueError('invalid GINH or CRC')
    return p
def convert(src,dst,threshold=128):
    from PIL import Image,ImageOps
    source=ImageOps.exif_transpose(Image.open(src)).convert('RGBA')
    source=Image.alpha_composite(Image.new('RGBA',source.size,'white'),source).convert('L')
    source.thumbnail((250,122),Image.Resampling.LANCZOS)
    canvas=Image.new('L',(250,122),255);canvas.paste(source,((250-source.width)//2,(122-source.height)//2))
    p=bytearray(3813)
    for y in range(122):
        for x in range(250):
            bit=y*250+x
            if canvas.getpixel((x,y))<threshold:p[bit//8]|=0x80>>(bit%8)
    Path(dst).write_bytes(encode(p))
if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('input');a.add_argument('output');a.add_argument('--threshold',type=int,default=128);args=a.parse_args()
    if not 0<=args.threshold<=255:a.error('threshold: 0..255')
    convert(args.input,args.output,args.threshold)
