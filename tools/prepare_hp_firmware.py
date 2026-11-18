#!/usr/bin/env python3
"""Prepare the unchanged HP P1006 printer payload, verified by a pinned hash.

HP payload remains third-party binary data, separate from the bridge source license.
Only the build machine downloads it; the ESP never needs Internet to bootstrap HP.
"""
import argparse, hashlib, io, tarfile, urllib.request
from pathlib import Path
HASH='b9293c793ea8666b6ea9fd624c2bb1f00ef697c3e710cf1189396442df7ad2a2'
def valid(data):
    return len(data)==222891 and hashlib.sha256(data).hexdigest()==HASH

def prepare(out,source=None):
    out=Path(out)
    if out.exists() and valid(out.read_bytes()):return
    if source:
        dl=Path(source).read_bytes()
    else:
        with urllib.request.urlopen('https://www.quirinux.org/printers/sihpP1006.tar.gz',timeout=40) as r:
            archive=r.read(300*1024+1)
        if len(archive)>300*1024:raise ValueError('HP archive too large')
        with tarfile.open(fileobj=io.BytesIO(archive),mode='r:gz') as tar:
            member=tar.getmember('sihpP1006.img')
            if not member.isfile() or member.size>256*1024:raise ValueError('Invalid HP payload')
            src=tar.extractfile(member).read()
        if src[8:12]!=b'\x7fELF':raise ValueError('Invalid HP image')
        dl=b'\x1b%-12345X@PJL ENTER LANGUAGE=ACL\r\n'+b'\0\xac\xc0\xde'+(len(src)-8).to_bytes(4,'big')+src+b'\x1b%-12345X'
    if not valid(dl):raise ValueError('HP P1006 SHA-256 mismatch; refusing build')
    out.parent.mkdir(parents=True,exist_ok=True)
    tmp=out.with_suffix('.tmp');tmp.write_bytes(dl);tmp.replace(out)
    print('HP P1006: embedded payload verified (222891 bytes)')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);p.add_argument('--source');a=p.parse_args();prepare(a.out,a.source)
