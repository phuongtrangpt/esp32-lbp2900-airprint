#!/usr/bin/env python3
"""Collect the exact ESP32-S3 build images and a verifiable flash manifest."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import re
import subprocess
import sys
import argparse

ROOT=Path(__file__).resolve().parent.parent
BUILD=ROOT/'.pio/build/esp32s3'
OUT=ROOT/'firmware'

PROFILE_NAMES = {
    'hp_pc': 'HP P1006 PC OFFLOAD TEST ESP 32',
    'hp': 'HP LaserJet P1006 ESP 32',
    'hp_p1008': 'HP LaserJet P1008 TEST ESP 32',
    'canon': 'Canon LBP2900 ESP 32',
    'pixma': 'Canon PIXMA USB ESP 32',
    'standard': 'USB Printer PCL PS ESP 32',
}

def main():
    global BUILD,OUT
    parser=argparse.ArgumentParser();parser.add_argument('--env',default='esp32s3');parser.add_argument('--out',default='firmware')
    args=parser.parse_args();BUILD=ROOT/'.pio/build'/args.env;OUT=ROOT/args.out
    if args.env in PROFILE_NAMES:
        cache=(BUILD/'CMakeCache.txt').read_text()
        profile=re.search(r'^PRINTSERVER_PROFILE:[^=]+=(.+)$',cache,re.M)
        if not profile or profile.group(1).strip()!=args.env:
            raise SystemExit('Refusing to package a firmware built for the wrong printer profile')
    names=['bootloader.bin','partitions.bin','firmware.bin']
    for name in names:
        if not (BUILD/name).is_file(): raise SystemExit(f'Missing build artifact: {name}; run pio run -e esp32s3')
    # Do not package firmware with a locally embedded Wi-Fi secret.
    header=ROOT/'src/local_config.h'
    if header.exists():
        text=header.read_text()
        for key in ('WIFI_SSID','WIFI_PASSWORD'):
            if f'#define {key} ""' not in text:
                raise SystemExit('Refusing to package firmware built with embedded Wi-Fi. Clear .env and rebuild.')
    OUT.mkdir(parents=True,exist_ok=True)
    for name in names: shutil.copyfile(BUILD/name,OUT/name)
    if args.env in PROFILE_NAMES:
        app=(OUT/'firmware.bin').read_bytes()
        # IDF app descriptor follows the first image and segment headers.
        expected=('vietphuong_'+args.env).encode()
        project=app[80:112].split(b'\0',1)[0]
        if len(app)<288 or struct.unpack_from('<I',app,32)[0]!=0xabcd5432 or project!=expected:
            raise SystemExit('Wrong OTA application identity: refusing model package')
    # Reset OTA selection on USB flashing so a previous ota_1 cannot boot instead.
    shutil.copyfile(BUILD/'ota_data_initial.bin', OUT/'ota_data_initial.bin')
    names.append('ota_data_initial.bin')
    # Match and verify ota_0 in the binary partition table, rather than guess offsets.
    app_offset=None
    table=(OUT/'partitions.bin').read_bytes()
    for pos in range(0,len(table)-31,32):
        magic,kind,sub,offset,size,label,flags=struct.unpack('<HBBII16sI',table[pos:pos+32])
        if magic!=0x50aa: continue
        if kind==0 and sub==0x10: app_offset=offset
    if app_offset!=0x20000: raise SystemExit(f'Unexpected ota_0 offset: {app_offset}')
    settings=json.loads((BUILD/'flasher_args.json').read_text())['flash_settings']
    version=re.search(r'set\(PROJECT_VER \"([^\"]+)\"\)', (ROOT/'CMakeLists.txt').read_text()).group(1)
    if args.env in ("hp", "hp_pc"):
        version = (OUT/"firmware.bin").read_bytes()[48:80].split(b"\0",1)[0].decode("ascii")
    manifest={'chip':'esp32s3','board':'ESP32-S3 N16R8','version':version,
              'physical_printer_tested':False,'flash_size':settings['flash_size'],'flash_mode':settings['flash_mode'],'flash_freq':settings['flash_freq'],
              'profile':args.env,'name':PROFILE_NAMES.get(args.env, 'Print Server ESP 32'),
              'files':{'0x0':'bootloader.bin','0x8000':'partitions.bin','0xf000':'ota_data_initial.bin',hex(app_offset):'firmware.bin'}}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    for name in ['LICENSE','LICENSE.upstream-MIT','README.md','VALIDATION.md','BUILD_STATUS.md']:
        shutil.copyfile(ROOT/name, OUT/name)
    (OUT/'SHA256SUMS').write_text(''.join(hashlib.sha256((OUT/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names+['manifest.json']))
    print('Packaged firmware:',OUT)

if __name__=='__main__': main()
