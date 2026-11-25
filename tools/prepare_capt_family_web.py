#!/usr/bin/env python3
"""Stage immutable, checked CAPT family prototypes without compiling stable drivers."""
import argparse, hashlib, io, json, shutil, subprocess, sys, tempfile
from pathlib import Path
from urllib.request import urlopen
from zipfile import ZipFile
ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'c4d34ae1d280d0993fcdebc0c4d6732ae72b0c71'
TAG = 'capt-family-test2-c4d34ae'
DIGESTS = {
    'lbp6000': '11cf28125201ea86e45f67ed57408e8833c77f84524c0e8710744bfa99d76515',
    'lbp6200': 'b8b1efa9aca30d63d96b7e195dc49812e0e2c40f480cc9bf6a85954b93e69fc6',
    'lbp3010': '87a3eda80e88681e74e117e999f1a573024cf046657d864f410f601062d9338b',
}
def stage(profile, raw):
    if hashlib.sha256(raw).hexdigest() != DIGESTS[profile]:
        raise ValueError('CAPT prerelease ZIP checksum mismatch: '+profile)
    with tempfile.TemporaryDirectory() as tmp, ZipFile(io.BytesIO(raw)) as z:
        if any(Path(n).is_absolute() or '..' in Path(n).parts for n in z.namelist()):
            raise ValueError('Unsafe ZIP entry')
        z.extractall(tmp)
        folder=Path(tmp)/profile; source=folder/'firmware'
        manifest=json.loads((source/'manifest.json').read_text())
        app=(source/'firmware.bin').read_bytes()
        if ((folder/'BUILD_COMMIT.txt').read_text().strip()!=COMMIT
            or manifest['profile']!=profile or manifest['chip']!='esp32s3'
            or manifest['flash_size']!='16MB' or manifest['physical_printer_tested'] is not False
            or manifest['version']!='0.1.1-'+profile+'-test2'
            or app[80:112].split(b'\0')[0]!=('vietphuong_'+profile).encode()):
            raise ValueError('Wrong CAPT model, build or flash layout')
        dest=ROOT/'installer/public/firmware'/profile
        subprocess.run([sys.executable,str(ROOT/'tools/prepare_web_installer.py'),
            '--source',str(source),'--dest',str(dest)],check=True)
        # Community tests initially use USB; do not offer unverified OTA paths.
        web=json.loads((dest/'web-manifest.json').read_text())
        web.update(usbOnly=True,physicalPrinterTested=False,sourceCommit=COMMIT)
        (dest/'web-manifest.json').write_text(json.dumps(web,indent=2)+'\n')
        (dest/'ota-manifest.json').unlink()
        names=sorted(p.name for p in dest.iterdir() if p.is_file() and p.name!='SHA256SUMS')
        (dest/'SHA256SUMS').write_text(''.join(hashlib.sha256((dest/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names))
        print(profile+': pinned prototype staged; USB only')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archives-dir',type=Path);args=p.parse_args()
    for profile in DIGESTS:
        name=profile+'-USB-TEST.zip'
        if args.archives_dir: raw=(args.archives_dir/name).read_bytes()
        else:
            url='https://github.com/phuongtrangpt/esp32-lbp2900-airprint/releases/download/'+TAG+'/'+name
            with urlopen(url,timeout=60) as response:raw=response.read(5*1024*1024)
        stage(profile,raw)
if __name__=='__main__':main()
