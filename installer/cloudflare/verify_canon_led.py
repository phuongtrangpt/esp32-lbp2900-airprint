"""Verify the published Canon LED-only OTA without flashing any device."""
import hashlib,json,sys,urllib.request
from pathlib import Path
base=sys.argv[1].rstrip('/')
root=Path(__file__).resolve().parents[2]
pinned=json.loads((root/'installer/canon-led-release.json').read_text())
def get(path):return urllib.request.urlopen(urllib.request.Request(base+path,headers={'User-Agent':'VietPhuong-Canon-LED-Check/1.0'}),timeout=60)
with get('/firmware/canon/web-manifest.json') as r:
 web=json.load(r);assert web['version']==pinned['version'] and not web.get('usbOnly')
 assert [p['offset'] for p in web['builds'][0]['parts']]==[0,0xf000,0x20000]
with get('/firmware/canon/ota-manifest.json') as r:
 assert 'application/json' in r.headers.get('Content-Type','')
 ota=json.load(r);assert ota['profile']=='canon' and ota['version']==pinned['version'] and ota['file']=='firmware.bin'
with get('/firmware/canon/firmware.bin') as r:
 data=r.read(4194305);assert len(data)==ota['size'] and hashlib.sha256(data).hexdigest()==ota['sha256']
 assert data[0]==0xe9 and int.from_bytes(data[12:14],'little')==9
 assert data[48:80].split(b'\0')[0].decode()==pinned['version']
 assert data[80:112].split(b'\0')[0]==b'vietphuong_canon'
 part=next(p for p in web['builds'][0]['parts'] if p['offset']==0x20000)
 assert part['sha256']==ota['sha256']
print('LIVE PASS: Canon LBP2900 LED release USB/OTA versions, app identity, size and SHA-256 match.')
