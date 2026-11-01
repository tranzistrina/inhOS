#!/usr/bin/env python3
"""Package successfully compiled targets; never manufactures a missing image."""
from pathlib import Path
import hashlib, json, zipfile, argparse
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('targets',nargs='+');args=p.parse_args()
out=root/'build'/'release';out.mkdir(parents=True,exist_ok=True)
for target in args.targets:
    build=root/'.pio'/'build'/target
    required=['bootloader.bin','partitions.bin','firmware.bin','firmware.elf']
    for name in required:
        if not (build/name).is_file():raise SystemExit(f'Missing compiled artifact: {target}/{name}')
    fw=(build/'firmware.bin').read_bytes()
    if not fw or fw[0]!=0xe9 or len(fw)>0x300000:raise SystemExit('Invalid/oversize ESP image')
    image=bytearray(b'\xff'*(0x10000+len(fw)))
    for offset,name in [(0,'bootloader.bin'),(0x8000,'partitions.bin'),(0x10000,'firmware.bin')]:
        data=(build/name).read_bytes();image[offset:offset+len(data)]=data
    dst=out/target;dst.mkdir(exist_ok=True)
    (dst/'FIRST_INSTALL_0x0_UNTESTED_ALPHA.bin').write_bytes(image)
    for name in required:(dst/name).write_bytes((build/name).read_bytes())
manifest={'status':'compiled, hardware untested alpha','targets':args.targets,'sha256':{str(f.relative_to(out)):hashlib.sha256(f.read_bytes()).hexdigest() for f in out.rglob('*') if f.is_file()}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
with zipfile.ZipFile(root/'build'/'inhOS-alpha.zip','w',zipfile.ZIP_DEFLATED) as z:
    for f in out.rglob('*'):
        if f.is_file():z.write(f,'firmware/'+str(f.relative_to(out)))
    for f in root.rglob('*'):
        rel=f.relative_to(root)
        if f.is_file() and not any(x in ['.pio','.git','build','__pycache__','downloads','firmware-input'] for x in rel.parts) and f.suffix not in ['.s','.o']:
            z.write(f,'source/'+str(rel))
print(root/'build'/'inhOS-alpha.zip')
