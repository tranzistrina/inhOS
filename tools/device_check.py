#!/usr/bin/env python3
"""Hardware-assisted test. Connect to inhOS upload AP first. Does NOT wipe user files.
Creates/replaces only _test_app*.inh and _test_photo*.ginh; removes only these fixtures.
Save stdout and USB serial output. Sleep/wake timing and current require manual measurement.
"""
import argparse,json,urllib.request,urllib.parse,urllib.error,base64,time
from pathlib import Path
from ginh import encode
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',default='http://192.168.4.1');a=p.parse_args()
def request(path,args=None):
    data=None if args is None else urllib.parse.urlencode(args).encode()
    return urllib.request.urlopen(urllib.request.Request(a.url+path,data=data),timeout=30).read()
def upload(name,data):
    request('/begin',dict(name=name,size=len(data)))
    for offset in range(0,len(data),2048):request('/chunk',dict(offset=offset,data=base64.b64encode(data[offset:offset+2048]).decode()))
    request('/finish',{})
def diagnostic():print(request('/diagnostics').decode())
fixtures=[]
try:
    for i in range(8):
        code=f'значение = {i}\nочисти()\nтекст(4,30,"RAM test {i}")\nяви()\n'.encode();code+=b'#'+b'x'*(10240-len(code)-2)+b'\n'
        name=f'/_test_app{i}.inh';upload(name[1:],code);fixtures.append(name)
        assert request('/download?'+urllib.parse.urlencode(dict(name=name)))==code
    for i in range(5):
        bitmap=bytearray(3813);bitmap[i]=128;data=encode(bitmap);name=f'/_test_photo{i}.ginh';upload(name[1:],data);fixtures.append(name)
        assert request('/download?'+urllib.parse.urlencode(dict(name=name)))==data
    for cycle in range(3):
        for i in range(8):
            request('/run',dict(name=f'/_test_app{i}.inh'));time.sleep(1);request('/stop',{});diagnostic()
    before=request('/files')
    try:request('/delete',dict(name=fixtures[0],confirm='cancel'))
    except urllib.error.HTTPError as e:assert e.code==409
    else:raise AssertionError('unconfirmed deletion accepted')
    assert request('/files')==before
    corrupted=bytearray(encode(bytearray(3813)));corrupted[-1]^=16
    try:upload('_test_bad.ginh',corrupted)
    except urllib.error.HTTPError as e:assert e.code==400
    else:raise AssertionError('corrupt photo accepted')
    print('PASS actual HTTP uploads/downloads, 8 apps/5 photos, 24 sequential runs, diagnostics collected, corrupt-photo rejection and cancelled-delete guard')
finally:
    request('/stop',{})
    for name in fixtures:request('/delete',dict(name=name,confirm='delete'))
