#!/usr/bin/env python3
"""Client <-> real C++ firmware protocol engine, fault-injected byte stream."""
import gc
import hashlib
import os
from pathlib import Path
import select
import struct
import subprocess
import sys
import tempfile
import tracemalloc
import threading
import tty
import pty
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'desktop'))
from inhfs.client import Client, InhError, Cancelled, remote_path, prepare
from inhfs import protocol as p
from inhfs.images import encode

class Transport:
    def __init__(self,folder,binary="usb_host"):
        self.folder=folder
        self.binary=binary
        self.process=None
        self.drop=False;self.corrupt=False;self.bad_response=False;self.break_at=False
        self.start()
    def start(self):
        self.process=subprocess.Popen([str(ROOT/'build'/self.binary),str(self.folder)],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
    def restart(self):
        self.process.kill();self.process.wait();self.process.stdin.close();self.process.stdout.close();self.start()
    def write(self,data):
        raw=p.Decoder().feed(data)[0]
        op=p.unpack(raw)[0]
        if self.break_at and op==p.PUT_DATA:
            raise OSError('cable unplugged')
        if self.drop and op in (p.PUT_DATA,p.GET_DATA):
            self.drop=False
            # Send the request but lose its ACK to force duplicate suppression.
            self.process.stdin.write(data);self.process.stdin.flush()
            self._read_response()
            return len(data)
        if self.corrupt and op==p.PUT_DATA:
            self.corrupt=False
            broken=bytearray(raw);broken[21]^=1
            self.process.stdin.write(p.frame(broken))
        else:self.process.stdin.write(data)
        self.process.stdin.flush()
        return len(data)
    def _read_response(self):
        decoder=p.Decoder();out=[]
        while not out:
            if not select.select([self.process.stdout],[],[],2)[0]:raise OSError('emulator timed out')
            out=decoder.feed(os.read(self.process.stdout.fileno(),4096))
        return out[0]
    def read(self,n):
        if not select.select([self.process.stdout],[],[],.002)[0]:return b''
        data=os.read(self.process.stdout.fileno(),n)
        if not data:raise OSError('device disconnected')
        if self.bad_response:
            self.bad_response=False
            # Damage a response CRC without altering SLIP boundaries.
            data=bytearray(data)
            if len(data)>5:data[-3]^=1
            return bytes(data)
        return data
    def close(self):
        pass # Closing a virtual port does not power off its attached device.
    def shutdown(self):
        if self.process is not None:
            self.process.stdin.close()
            code=self.process.wait(timeout=3)
            self.process.stdout.close()
            if code != 0: raise AssertionError(f'Firmware host exited {code}; sanitizer/LSan failure')
            self.process=None

class UsbTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.root=Path(self.tmp.name);self.fs=self.root/'device';self.fs.mkdir()
        for name in ('nvs','firmware.bin','partitions.bin','settings.dat'):(self.fs/name).write_bytes(b'protected-'+name.encode())
        self.transport=Transport(self.fs);self.client=Client(self.transport);self.client.connect()
    def tearDown(self):
        self.client.close(False)
        if self.transport.process:self.transport.shutdown()
        self.tmp.cleanup()
    def source(self,name,data):
        f=self.root/name;f.write_bytes(data);return f
    def test_8_apps_5_photos_and_3MiB_book(self):
        for i in range(8):
            data=b'#'+bytes([65+i])*10238+b'\n'
            self.client.upload(self.source(f'a{i}.inh',data))
        for i in range(5):self.client.upload(self.source(f'p{i}.ginh',encode(bytes([i*16])*3812+b'\x00')))
        book=(b'Book UTF-8 text\n'*220000)[:3*1024*1024]
        self.client.upload(self.source('large.txt',book))
        self.assertEqual(len(self.client.files()),14)
        for item in self.client.files():
            target=self.root/('copy-'+item['name'][1:])
            self.client.download(item['name'],target)
            self.assertEqual(hashlib.sha256(target.read_bytes()).digest(),hashlib.sha256((self.fs/item['name'][1:]).read_bytes()).digest())
        self.assertEqual((self.fs/'book.txt').read_bytes(),book)
        self.client.sleep_photo('/p3.ginh');self.assertEqual(self.client.sleep_photo(),'/p3.ginh')
        self.client.sleep_photo('');self.assertEqual(self.client.sleep_photo(),'')
    def test_crc_retries_and_duplicate_ack(self):
        source=self.source('a.inh',b'#'+b'a'*4095)
        self.transport.corrupt=True;self.client.upload(source)
        self.transport.drop=True;self.client.upload(source)
        self.transport.drop=True;self.client.download('/a.inh',self.root/'out')
        self.transport.bad_response=True;self.client.ping()
        self.assertEqual((self.root/'out').read_bytes(),source.read_bytes())
    def test_cancel_cable_reset_preserves_old(self):
        source=self.source('a.inh',b'# old\n');self.client.upload(source)
        source.write_bytes(b'# new\n'*1000)
        def cancel(n,total):self.client.cancel_transfer()
        with self.assertRaises(Cancelled):self.client.upload(source,progress=cancel)
        self.assertEqual((self.fs/'a.inh').read_bytes(),b'# old\n')
        self.assertFalse((self.fs/'usb-upload.tmp').exists())
        self.client.request(p.PUT_BEGIN,struct.pack('<II',6,zlib.crc32(b'# new\n'))+b'/a.inh')
        self.client.request(p.PUT_DATA,b'# n',block=0)
        self.transport.restart()
        with self.assertRaises(InhError):self.client.ping()
        self.assertEqual((self.fs/'a.inh').read_bytes(),b'# old\n')
        self.assertFalse((self.fs/'usb-upload.tmp').exists())
        self.client=Client(self.transport);self.client.connect()
        self.transport.break_at=True
        with self.assertRaises(InhError):self.client.upload(source)
        self.assertEqual((self.fs/'a.inh').read_bytes(),b'# old\n')
        import time
        time.sleep(15.2) # Real firmware transfer-inactivity timeout in harness
        self.assertFalse((self.fs/'usb-upload.tmp').exists())
    def test_invalid_and_delete_protection(self):
        for path in ('/../nvs','/nvs','/firmware.bin','/book.idx','/a/b.inh','/a.inh\x00'):
            with self.assertRaises(InhError):remote_path(path)
            with self.assertRaises(InhError):self.client.request(p.DELETE,path.encode())
        self.client.upload(self.source('a.inh',b'# old'))
        with self.assertRaises(InhError):self.client.upload(self.source('a.inh',b'\xff'))
        self.assertEqual((self.fs/'a.inh').read_bytes(),b'# old')
        bad=bytearray(encode(bytes(3813)));bad[60]^=1
        # Bypass client validation: test firmware-side image validation.
        self.client.request(p.PUT_BEGIN,struct.pack('<II',len(bad),zlib.crc32(bad))+b'/bad.ginh')
        for offset in range(0,len(bad),512):self.client.request(p.PUT_DATA,bad[offset:offset+512],offset)
        with self.assertRaises(InhError):self.client.request(p.PUT_END)
        self.assertFalse((self.fs/'bad.ginh').exists())
        self.client.upload(self.source('book.txt',b'book\n'))
        (self.fs/'book.idx').write_bytes(b'index')
        self.client.delete('/book.txt');self.assertFalse((self.fs/'book.idx').exists())
        with self.assertRaises(InhError):self.client.wipe()
        with self.assertRaises(InhError):self.client.request(p.WIPE,b'yes')
        self.client.wipe(True);self.assertEqual(self.client.files(),[])
        for name in ('nvs','firmware.bin','partitions.bin','settings.dat'):
            self.assertEqual((self.fs/name).read_bytes(),b'protected-'+name.encode())
    def test_whole_file_crc_and_download_local_safety(self):
        self.client.upload(self.source('a.inh',b'# old'))
        self.client.request(p.PUT_BEGIN,struct.pack('<II',5,123)+b'/a.inh')
        self.client.request(p.PUT_DATA,b'# new')
        # Full-file CRC NACK is fatal for this transfer, not a frame retry.
        with self.assertRaises(InhError):self.client.request(p.PUT_END,timeout=.01,retries=2)
        self.assertEqual((self.fs/'a.inh').read_bytes(),b'# old')
        self.client.ping()
        target=self.source('out',b'local old')
        def cancel(n,total):self.client.cancel_transfer()
        with self.assertRaises(Cancelled):self.client.download('/a.inh',target,cancel)
        self.assertEqual(target.read_bytes(),b'local old')
    def test_repeated_sessions(self):
        # ASan retains freed objects in quarantine: RSS is not live heap there.
        # First exercise sanitized server; LSan checks it on clean process exit.
        for i in range(80):
            self.client.session=0;self.client.seq=1
            self.client.request(p.HELLO,b'INHFS/1')
            self.client.upload(self.source('asan.inh',b'# sanitized session\n'*100))
            self.client.download('/asan.inh',self.root/'asan-out')
        self.client.close(False);self.transport.shutdown()
        self.transport=Transport(self.fs,'usb_host_profile')
        self.client=Client(self.transport);self.client.connect()
        source=self.source('repeat.inh',b'# repeated session\n'*100)
        self.client.upload(source)
        def rss():
            text=Path(f'/proc/{self.transport.process.pid}/status').read_text()
            return int(next(line.split()[1] for line in text.splitlines() if line.startswith('VmRSS:')))
        before_rss=rss()
        before_fds=len(list(Path(f'/proc/{self.transport.process.pid}/fd').iterdir()))
        tracemalloc.start()
        for i in range(10):self.client.ping()
        gc.collect();before=tracemalloc.get_traced_memory()[0]
        for i in range(80):
            self.client.session=0;self.client.seq=1
            self.client.request(p.HELLO,b'INHFS/1')
            self.client.info();self.client.files()
            self.client.upload(source)
            self.client.download('/repeat.inh',self.root/'repeat-out')
        gc.collect();after=tracemalloc.get_traced_memory()[0]
        tracemalloc.stop();self.assertLess(after-before,100000)
        self.assertLessEqual(rss()-before_rss,1024)
        self.assertEqual(len(list(Path(f'/proc/{self.transport.process.pid}/fd').iterdir())),before_fds)
        print(f'80 sessions: Python retained delta={after-before} bytes, host RSS delta={rss()-before_rss} KiB; descriptors unchanged')
    def test_entry_points(self):
        for entry in ('windows.py','macos.py'):
            subprocess.run([sys.executable,str(ROOT/'desktop'/entry),'--self-test'],check=True)
        subprocess.run([sys.executable,str(ROOT/'desktop/inhfs_cli.py'),'--help'],check=True,stdout=subprocess.DEVNULL)
        with tempfile.TemporaryDirectory() as temp:
            from PIL import Image
            for ext in ('png','jpg','webp'):
                source=self.root/('input.'+ext);Image.new('RGB',(600,400),'black').save(source)
                converted,remote=prepare(source,temp);self.assertEqual(converted.stat().st_size,3833)
            fb=self.source('test.fb2',b'<FictionBook><body><section><p>Hello</p></section></body></FictionBook>')
            converted,remote=prepare(fb,temp);self.assertEqual(remote,'/book.txt');self.assertIn('Hello',converted.read_text())
    def test_pyserial_pty(self):
        # Real pyserial open/read/write/exclusive/close against an OS pseudo-TTY.
        import serial
        master,slave=pty.openpty();tty.setraw(slave)
        stop=threading.Event()
        def bridge():
            while not stop.is_set():
                ready,_,_=select.select([master,self.transport.process.stdout],[],[],.02)
                for stream in ready:
                    try:
                        data=os.read(stream if isinstance(stream,int) else stream.fileno(),4096)
                        if not data:return
                        if stream==master:
                            self.transport.process.stdin.write(data);self.transport.process.stdin.flush()
                        else:os.write(master,data)
                    except OSError:return
        worker=threading.Thread(target=bridge,daemon=True);worker.start()
        live=Client()
        try:
            live.connect(os.ttyname(slave))
            live.upload(self.source('pty.inh',b'# real serial path\n'*200))
            live.download('/pty.inh',self.root/'pty-out')
            self.assertEqual((self.root/'pty-out').read_bytes(),(self.root/'pty.inh').read_bytes())
            live.close(False)
            cli=[sys.executable,str(ROOT/'desktop/inhfs_cli.py'),'--port',os.ttyname(slave)]
            for args in (['info'],['list'],['upload',str(self.root/'pty.inh')],
                         ['download','/pty.inh',str(self.root/'cli-out')],
                         ['set-sleep-photo','builtin'],['delete','/pty.inh'],['disconnect']):
                completed=subprocess.run(cli+args,capture_output=True,timeout=30)
                self.assertEqual(completed.returncode,0,completed.stderr.decode())
            self.assertEqual((self.root/'cli-out').read_bytes(),(self.root/'pty.inh').read_bytes())
        finally:
            live.close(False);stop.set();worker.join(2);os.close(master);os.close(slave)
    def test_decoder_noise_overflow(self):
        raw=p.packet(p.PING,3,session=8,data=b'\xc0\xdb')
        d=p.Decoder();self.assertEqual(d.feed(b'ROM boot\n'+b'\xc0'+b'x'*1000+p.frame(raw)),[raw])
        with self.assertRaises(ValueError):p.unpack(raw[:-1]+bytes([raw[-1]^1]))

if __name__=='__main__':unittest.main(verbosity=2)
