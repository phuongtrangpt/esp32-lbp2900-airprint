#!/usr/bin/env python3
"""Build same-origin web flash assets from a checked, credential-free package.

ESP Web Tools needs image headers patched using esptool merge_bin. Split the
patched image around NVS so installing without erasing can retain Wi-Fi settings.
The byte range [0x9000, 0xf000) is NEVER included in a web flash part.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import argparse

ROOT=Path(__file__).resolve().parent.parent
SOURCE=ROOT/'firmware'
DEST=ROOT/'installer/public/firmware'

def main():
    global SOURCE,DEST
    parser=argparse.ArgumentParser();parser.add_argument('--source',default='firmware');parser.add_argument('--dest',default='installer/public/firmware')
    args=parser.parse_args();SOURCE=ROOT/args.source;DEST=ROOT/args.dest
    manifest=json.loads((SOURCE/'manifest.json').read_text())
    for line in (SOURCE/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split(None,1);name=name.strip()
        if Path(name).name!=name or hashlib.sha256((SOURCE/name).read_bytes()).hexdigest()!=digest:
            raise SystemExit('Invalid package checksum')
    if manifest['chip']!='esp32s3' or manifest['flash_size']!='16MB': raise SystemExit('Wrong target')
    DEST.mkdir(parents=True,exist_ok=True)
    merged=DEST/'firmware-merged.bin'
    cmd=[sys.executable,'-m','esptool','--chip','esp32s3','merge_bin','-o',str(merged),
         '--flash_mode',manifest['flash_mode'],'--flash_freq',manifest['flash_freq'],'--flash_size',manifest['flash_size']]
    for offset,name in manifest['files'].items(): cmd.extend([offset,str(SOURCE/name)])
    subprocess.run(cmd,check=True)
    data=merged.read_bytes()
    # Exact partition boundaries; leave NVS and PHY calibration sectors alone.
    parts=[('system.bin',0,0x9000),('ota_data_initial.bin',0xf000,0x11000),
           ('firmware.bin',0x20000,len(data))]
    for name,start,end in parts:
        assert 0<=start<end<=len(data)
        assert end<=0x9000 or start>=0xf000, 'part overlaps credentials'
        (DEST/name).write_bytes(data[start:end])
    assert (DEST/'ota_data_initial.bin').read_bytes()==(SOURCE/'ota_data_initial.bin').read_bytes()
    assert (DEST/'firmware.bin').read_bytes()==(SOURCE/'firmware.bin').read_bytes()
    web={'hpBootstrapEmbedded':manifest['profile'] in ('hp','hp_p1008','esp32s3'),'name':manifest.get('name','Print Server ESP 32'),'version':manifest['version'],'new_install_prompt_erase':True,
         'new_install_improv_wait_time':20,
         'builds':[{'chipFamily':'ESP32-S3','serialType':'uart','parts':[{'path':n,'offset':s,'sha256':hashlib.sha256((DEST/n).read_bytes()).hexdigest()} for n,s,e in parts]}]}
    (DEST/'web-manifest.json').write_text(json.dumps(web,indent=2)+'\n')
    app=(DEST/'firmware.bin').read_bytes()
    if manifest['profile'] in ('hp','hp_p1008','esp32s3'):
        from prepare_hp_firmware import valid
        hp=(ROOT/'src/hp_p1006.dl').read_bytes()
        if not valid(hp) or hp not in app:raise SystemExit('HP firmware is not embedded in application')
    notes=(ROOT/'RELEASE_NOTES.txt').read_text().strip()[:180]
    if manifest['profile']=='hp' and manifest['version']=='0.9.10-hp-raw12':
        notes='HP P1006 RAW12: in từ điện thoại qua IPP, Windows qua RAW 9100. Đã thử lệnh 4 trang; đang theo dõi độ ổn định dài hạn.'
    ota={'notes':notes,'version':manifest['version'],'profile':manifest['profile'],'file':'firmware.bin','size':len(app),'sha256':hashlib.sha256(app).hexdigest()}
    (DEST/'ota-manifest.json').write_text(json.dumps(ota,indent=2,ensure_ascii=False)+'\n')
    names=[n for n,s,e in parts]+['firmware-merged.bin','web-manifest.json','ota-manifest.json']
    (DEST/'SHA256SUMS').write_text(''.join(hashlib.sha256((DEST/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names))
    for name in ['LICENSE','LICENSE.upstream-MIT','VALIDATION.md']:
        shutil.copyfile(ROOT/name,DEST/name)
    print('Web installer assets ready; NVS gap excluded:',DEST)

if __name__=='__main__': main()
