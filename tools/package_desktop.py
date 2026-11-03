#!/usr/bin/env python3
"""Create portable source clients, including executable Mac launcher metadata."""
from pathlib import Path
import hashlib
import zipfile
root=Path(__file__).resolve().parents[1]
out=root/'build/inhOS-usb-clients.zip';out.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
    for file in sorted((root/'desktop').rglob('*')):
        if not file.is_file() or any(x in ('.venv','__pycache__') for x in file.parts):continue
        info=zipfile.ZipInfo('inhOS-USB/'+str(file.relative_to(root/'desktop')))
        info.create_system=3
        info.external_attr=(0o100755 if file.suffix=='.command' else 0o100644)<<16
        info.compress_type=zipfile.ZIP_DEFLATED
        data=file.read_bytes()
        if file.suffix=='.bat':data=data.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
        z.writestr(info,data)
    z.write(root/'docs/INHFS-1.md','inhOS-USB/INHFS-1.md')
    z.write(root/'docs/VERIFICATION-0.3.0.md','inhOS-USB/VERIFICATION-0.3.0.md')
print(out,hashlib.sha256(out.read_bytes()).hexdigest())
