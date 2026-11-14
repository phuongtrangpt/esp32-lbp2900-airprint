#!/usr/bin/env python3
"""Optional independent foo2xqx comparison. Pass path to upstream executable."""
import pathlib, subprocess, sys, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
reference=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as td:
    prefix=pathlib.Path(td)/'hp'
    subprocess.run([str(root/'host/hp_host'),str(prefix)],check=True)
    for paper, (w,h,code) in enumerate([(21000*600//2540,29700*600//2540,9),(5100,6600,1)]):
        iw,ih=w-192,h-192
        bpl=(iw+7)//8
        pbm=bytearray()
        for page in range(2):
            pbm.extend(f'P4\n{iw} {ih}\n'.encode())
            for y in range(ih):
                row=bytearray(bpl)
                for x in range(iw):
                    if ((x+96)//37+(y+96)//29+page)%3==0:
                        row[x//8] |= 0x80>>(x%8)
                pbm.extend(row)
        result=subprocess.run([str(reference),'-r600x600',f'-p{code}'],input=pbm,stdout=subprocess.PIPE,check=True).stdout
        actual=pathlib.Path(f'{prefix}-{paper}.xqx').read_bytes()
        def protocol(data):
            return data[data.index(b',XQX'):data.rindex(b'\x1b%-12345X@PJL EOJ')]
        a,b=protocol(actual),protocol(result)
        assert a==b, f'paper {paper}: XQX differs at '+str(next((i for i,(x,y) in enumerate(zip(a,b)) if x!=y),min(len(a),len(b))))
        print(f'paper {paper}: {len(a)} XQX bytes match independent foo2xqx exactly, two pages')
