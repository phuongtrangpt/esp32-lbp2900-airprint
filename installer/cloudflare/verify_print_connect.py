"""Verify public product download, brand assets and online firmware after deployment."""
import hashlib,json,sys,urllib.request
from pathlib import Path
base=sys.argv[1].rstrip('/')
root=Path(__file__).resolve().parents[2]
pinned=json.loads((root/'installer/print-connect-release.json').read_text())
def get(path):
 req=urllib.request.Request(base+path,headers={'User-Agent':'VietPhuong-Web-IntegrationTest/1.0'})
 return urllib.request.urlopen(req,timeout=60)
with get('/print-connect/') as r:
 text=r.read().decode();assert pinned['version'] in text and 'HP LaserJet P1006 - Viet Phuong' in text
with get('/software/'+pinned['installer']) as r:
 digest=hashlib.sha256();size=0;first=True
 while True:
  block=r.read(65536)
  if not block:break
  if first:assert block[:2]==b'MZ';first=False
  digest.update(block);size+=len(block)
 assert digest.hexdigest()==pinned['assets'][pinned['installer']]['sha256'];assert size==pinned['assets'][pinned['installer']]['size']
with get('/brand/logo.png') as r:assert hashlib.sha256(r.read()).hexdigest()==hashlib.sha256((root/'installer/public/brand/logo.png').read_bytes()).hexdigest()
with get('/brand/favicon.ico') as r:assert r.read(4)==b'\0\0\1\0'
with get('/firmware/hp_pc/web-manifest.json') as r:
 m=json.load(r);assert m['version']==json.loads((root/'installer/android-release.json').read_text())['firmware']['version'];assert m['usbOnly'] is False;assert len(m['builds'][0]['parts'])==3
print('LIVE PASS: Windows installer exact SHA-256, installation guide, supplied brand logo/favicon, Print Connect online firmware.')

android=json.loads((root/'installer/android-app-release.json').read_text())
with get('/print-connect/android/') as r:
 text=r.read().decode();assert android['version'] in text and 'HP P1006 - Viet Phuong' in text
with get('/software/'+android['apk']['file']) as r:
 digest=hashlib.sha256();size=0;first=True
 while True:
  block=r.read(65536)
  if not block:break
  if first:assert block[:2]==b'PK';first=False
  digest.update(block);size+=len(block)
 assert digest.hexdigest()==android['apk']['sha256'];assert size==android['apk']['size']
print('LIVE PASS: Android APK exact SHA-256, Android setup guide, shared Print Connect firmware download-card update.')

with get('/firmware/hp_pc/ota-manifest.json') as r:
 assert 'application/json' in r.headers.get('Content-Type',''), 'OTA endpoint must return JSON, never the website HTML fallback'
 ota=json.load(r)
 assert ota['profile']=='hp_pc' and ota['version']==json.loads((root/'installer/android-release.json').read_text())['firmware']['version'] and ota['file']=='firmware.bin'
with get('/firmware/hp_pc/firmware.bin') as r:
 data=r.read(4194305);assert len(data)==ota['size'] and hashlib.sha256(data).hexdigest()==ota['sha256']
 assert data[0]==0xe9 and int.from_bytes(data[12:14],'little')==9
 assert data[48:80].split(b'\0')[0].decode()==ota['version']
 assert data[80:112].split(b'\0')[0]==b'vietphuong_hp_pc'
print('LIVE PASS: HP P1006 Print Connect OTA JSON, app size/SHA-256, ESP32-S3 target, project identity and version match.')
