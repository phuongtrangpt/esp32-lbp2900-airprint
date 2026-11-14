#!/usr/bin/env python3
"""Flash a packaged build with checksum verification. Never runs on import."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser(description='Flash LBP2900 preview firmware to ESP32-S3 N16R8')
    parser.add_argument('--port',required=True,help='COM5, /dev/ttyUSB0, etc.')
    parser.add_argument('--build-dir',type=Path,default=ROOT/'firmware')
    parser.add_argument('--reset-wifi',action='store_true',help='Erase all NVS settings, including saved Wi-Fi')
    args=parser.parse_args()
    base=[sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port]
    if args.reset_wifi:
        subprocess.run(base+['erase_region','0x9000','0x6000'],check=True)
        print('NVS erased. Reset the board and configure Wi-Fi through COM at 115200 baud.')
        return
    folder=args.build_dir.resolve()
    sums={}
    for line in (folder/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split(None,1); sums[name.strip()]=digest
    for name,digest in sums.items():
        if Path(name).name!=name: raise SystemExit('Invalid checksum filename')
        if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=digest:
            raise SystemExit('Checksum mismatch: '+name)
    manifest=json.loads((folder/'manifest.json').read_text())
    if manifest['chip']!='esp32s3' or manifest['flash_size']!='16MB': raise SystemExit('Unsupported image manifest')
    command=base+['write_flash','--flash_mode',manifest['flash_mode'],'--flash_freq',manifest['flash_freq'],
                  '--flash_size',manifest['flash_size']]
    for offset,name in manifest['files'].items():
        if Path(name).name!=name or name not in sums: raise SystemExit('Invalid image filename')
        command += [offset,str(folder/name)]
    subprocess.run(command,check=True)
    print('Flashed. Open the COM monitor at 115200 baud, LF line endings, to set Wi-Fi on first boot.')

if __name__=='__main__': main()
