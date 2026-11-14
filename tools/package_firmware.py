#!/usr/bin/env python3
"""Collect the exact ESP32-S3 build images and a verifiable flash manifest."""
import hashlib
import json
from pathlib import Path
import shutil
import struct

ROOT=Path(__file__).resolve().parent.parent
BUILD=ROOT/'.pio/build/esp32s3'
OUT=ROOT/'firmware'

def main():
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
    OUT.mkdir(exist_ok=True)
    for name in names: shutil.copyfile(BUILD/name,OUT/name)
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
    manifest={'chip':'esp32s3','board':'ESP32-S3 N16R8','version':'0.1.0-capt-preview',
              'physical_printer_tested':False,'flash_size':settings['flash_size'],'flash_mode':settings['flash_mode'],'flash_freq':settings['flash_freq'],
              'files':{'0x0':'bootloader.bin','0x8000':'partitions.bin','0xf000':'ota_data_initial.bin',hex(app_offset):'firmware.bin'}}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    for name in ['LICENSE','LICENSE.upstream-MIT','README.md','VALIDATION.md','BUILD_STATUS.md']:
        shutil.copyfile(ROOT/name, OUT/name)
    (OUT/'SHA256SUMS').write_text(''.join(hashlib.sha256((OUT/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names+['manifest.json']))
    print('Packaged firmware:',OUT)

if __name__=='__main__': main()
