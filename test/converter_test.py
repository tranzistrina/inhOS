from pathlib import Path
import sys,tempfile
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ginh import encode,decode,convert
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    for ext in ['png','jpg']:
        image=Image.new('RGB',(500,244),'white');image.paste((0,0,0),(0,0,250,244));image.save(root/('input.'+ext))
        convert(root/('input.'+ext),root/'out.ginh');data=(root/'out.ginh').read_bytes();pixels=decode(data)
        assert len(data)==3833 and len(pixels)==3813
        assert pixels[0]&128 and not pixels[31]&128
    for data in [b'',b'GINH',b'\0'*3833]:
        try:decode(data)
        except ValueError:pass
        else:raise AssertionError('invalid file accepted')
print('PASS: PNG/JPG converter, packed pixels, length, validation')
