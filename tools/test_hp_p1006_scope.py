#!/usr/bin/env python3
"""Confirm Canon/common code remains unchanged outside explicit HP-only guards."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--base',required=True);args=p.parse_args()
root=Path(__file__).resolve().parent.parent

def before(name):
    return subprocess.check_output(['git','show',args.base+':'+name],cwd=root)

paths=subprocess.check_output(['git','ls-tree','-r','--name-only',args.base],cwd=root,text=True).splitlines()
protected=[n for n in paths if n.startswith(('components/capt/','components/airprint/','web/','installer/')) or n in (
    'components/printers/canon_lbp2900.c','components/printers/mono_raster.h',
    'src/main.c','src/airprint_esp.c','src/status_led.c','src/status_led_pattern.h','src/print_job.c',
    '.github/workflows/build.yml','.github/workflows/cloudflare-pages.yml')]
for name in protected:
    assert (root/name).read_bytes()==before(name),name+' changed'
cmake=(root/'CMakeLists.txt').read_text()
guard='if(PRINTSERVER_PROFILE STREQUAL "hp")\n  set(PROJECT_VER "0.9.10-hp-test1")\nendif()\n'
assert cmake.count(guard)==1
assert cmake.replace(guard,'').encode()==before('CMakeLists.txt')
usb=(root/'src/usb_printer.c').read_text()
start='#if !defined(PRINTSERVER_PROFILE_HP) || defined(PRINTSERVER_PROFILE_HP_P1008)\n'
end='\n#endif\n        // Dedicated P1006 streams may NAK during warm-up/paper wait. They\n        // still check cancellation/disconnect above and log stalls below.'
assert usb.count(start)==usb.count(end)==2
assert usb.replace(start,'').replace(end,'').encode()==before('src/usb_printer.c')
print(f'PASS: {len(protected)} protected files unchanged; Canon USB/CMake paths identical after HP-only preprocessing')
