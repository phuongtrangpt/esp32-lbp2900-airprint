#!/usr/bin/env python3
"""Build actual family adapter/protocol with ASan/UBSan and a virtual USB model."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='capt-family-') as tmp:
    # Independent upstream decompressor, namespaced without editing its source.
    inc=['-Icomponents/airprint/include','-Icomponents/capt_family/include','-Icomponents/capt_family/vendor','-Ihost']
    flags=['-O2','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-pie','-no-pie']
    src=['host/capt_family_host.c','host/capt_reference/hiscoa-decompress.c','components/printers/printers.c','components/printers/canon_capt_family.c','components/capt_family/capt_family_transport.c']+[str(p.relative_to(ROOT)) for p in (ROOT/'components/capt_family/vendor').glob('*.c')]
    for profile in ['LBP6000','LBP6200','LBP3010']:
        target=str(Path(tmp)/profile)
        cmd=['cc',*flags,*inc,'-DPRINTSERVER_PROFILE_CAPT_FAMILY=1',f'-DPRINTSERVER_PROFILE_{profile}=1','-Dhiscoa_default_params=cf_hiscoa_default_params','-Dhiscoa_params=cf_hiscoa_params',*src,'-Wl,--wrap=clock_gettime,--wrap=nanosleep','-o',target]
        subprocess.run(cmd,cwd=ROOT,check=True)
        result=subprocess.run([target],cwd=ROOT,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'},stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if result.returncode:
            print(result.stdout);print(result.stderr[-12000:]);raise SystemExit(result.returncode)
        print(profile+': '+result.stdout.strip())
