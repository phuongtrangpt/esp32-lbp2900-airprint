#!/usr/bin/env python3
"""Stage immutable, checked CAPT family prototypes without compiling stable drivers."""
import argparse, hashlib, io, json, shutil, subprocess, sys, tempfile
from pathlib import Path
from urllib.request import urlopen
from zipfile import ZipFile
ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'da12cf97456cf3a6e0edf2effc7e818e7a9145c5'
TAG = 'capt-family-test1-da12cf9'
DIGESTS = {
    'lbp6000': 'bc9ff507b0ac72cda0e6420fdccae40548e37e1443398de4ad81f26d2d70979d',
    'lbp6200': '0676a42085e60ba9124a97d44091067972ebad04bbd793e7676aa3189a625a6a',
    'lbp3010': '71ecfbfc1e469c5ad3a612eb00a1491dc766a8ddbeb8d7610ff987b22c89b666',
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
            or manifest['version']!='0.1.0-'+profile+'-test1'
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
