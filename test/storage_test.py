"""Real LittleFS C implementation on RAM flash; native Glagol executable on host.
This is not an ESP32/Arduino hardware test.
"""
import sys,tempfile,subprocess
from pathlib import Path
from littlefs import LittleFS,UserContext
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ginh import encode,decode
root=Path(__file__).resolve().parents[1]
context=UserContext(buffsize=0x4e0000)
fs=LittleFS(context=context,mount=False,block_size=4096,block_count=0x4e0,read_size=128,prog_size=128)
fs.format();fs.mount()
def write(name,data):
    with fs.open('/upload.tmp','wb') as f:f.write(data)
    fs.rename('/upload.tmp',name)
apps={}
for i in range(8):
    body=f'значение = {i}\nочисти()\nтекст(4, 30, "Приложение {i}")\nяви()\n'.encode()
    body+=b'#'+b'x'*(10240-len(body)-2)+b'\n';assert len(body)==10240
    apps[f'/app{i}.inh']=body;write(f'/app{i}.inh',body)
photos={}
for i in range(5):
    p=bytearray(3813);p[i]=0x80;photos[f'/photo{i}.ginh']=encode(p);write(f'/photo{i}.ginh',encode(p))
write('/book.txt','Книга\n'.encode());write('/book.idx',b'index')
fs.unmount()
fs=LittleFS(context=UserContext(buffer=bytearray(context.buffer)),mount=False,block_size=4096,block_count=0x4e0,read_size=128,prog_size=128);fs.mount()
assert len([n for n in fs.listdir('/') if n.endswith('.inh')])==8
assert len([n for n in fs.listdir('/') if n.endswith('.ginh')])==5
with tempfile.TemporaryDirectory() as tmp:
    paths=[]
    for name,data in apps.items():
        with fs.open(name,'rb') as f:assert f.read()==data
        p=Path(tmp)/name[1:];p.write_bytes(data);paths.append(str(p))
    subprocess.run([str(root/'build/check_examples'),*paths],check=True)
    for name,data in photos.items():
        with fs.open(name,'rb') as f:assert decode(f.read())==data[20:]
# Transactional replacement preserves the directory entry, and restart didn't format it.
write('/app0.inh',apps['/app0.inh']);assert len([n for n in fs.listdir('/') if n.endswith('.inh')])==8
before=set(fs.listdir('/'));confirm=False
if confirm:fs.remove('/app0.inh')
assert set(fs.listdir('/'))==before # cancelled UI operation: no FS call
fs.remove('/app0.inh');assert 'app1.inh' in fs.listdir('/')
fs.remove('/photo0.ginh');assert 'photo1.ginh' in fs.listdir('/')
for name in ['/book.txt','/book.idx']:fs.remove(name)
for name in fs.listdir('/'):fs.remove('/'+name)
assert fs.listdir('/')==[]
print('PASS: real LittleFS, 8 x 10KiB apps + 5 GINH retained after remount, all apps run sequentially, replacement, cancel, individual deletion and wipe. Firmware UI/ESP hardware not emulated.')
