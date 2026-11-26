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
 m=json.load(r);assert m['version']==pinned['version']+'-hp-print-connect';assert m['usbOnly'];assert len(m['builds'][0]['parts'])==3
print('LIVE PASS: Windows installer exact SHA-256, installation guide, supplied brand logo/favicon, Print Connect online firmware.')
