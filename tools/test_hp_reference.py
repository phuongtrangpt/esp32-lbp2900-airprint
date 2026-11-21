#!/usr/bin/env python3
"""Optional independent foo2xqx comparison. Pass path to upstream executable."""
import argparse, pathlib, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('reference', type=pathlib.Path)
parser.add_argument('host', nargs='?', type=pathlib.Path, default=root/'host/hp_host')
parser.add_argument('--paper-count', type=int, choices=(3, 4), default=4)
args = parser.parse_args()
reference = args.reference.resolve()
host = args.host.resolve()
with tempfile.TemporaryDirectory() as td:
    prefix=pathlib.Path(td)/'hp'
    subprocess.run([str(host),str(prefix)],check=True)
    papers = [(21000*600//2540,29700*600//2540,9),(5100,6600,1),(14800*600//2540,21000*600//2540,11),(10500*600//2540,14800*600//2540,70)]
    for paper, (w,h,code) in enumerate(papers[:args.paper_count]):
        iw,ih=w-192,h-192
        bpl=(iw*2+7)//8
        pbm=bytearray()
        for page in range(2):
            pbm.extend(f'P4\n{iw*2} {ih}\n'.encode())
            for y in range(ih):
                row=bytearray(bpl)
                for x in range(iw*2):
                    if ((x//2+96)//37+(y+96)//29+page)%3==0:
                        row[x//8] |= 0x80>>(x%8)
                pbm.extend(row)
        result=subprocess.run([str(reference),'-r1200x600',f'-p{code}'],input=pbm,stdout=subprocess.PIPE,check=True).stdout
        actual=pathlib.Path(f'{prefix}-{paper}.xqx').read_bytes()
        def protocol(data):
            return data[data.index(b',XQX'):data.rindex(b'\x1b%-12345X@PJL EOJ')]
        a,b=protocol(actual),protocol(result)
        assert a==b, f'paper {paper}: XQX differs at '+str(next((i for i,(x,y) in enumerate(zip(a,b)) if x!=y),min(len(a),len(b))))
        print(f'paper {paper}: {len(a)} XQX bytes match independent foo2xqx exactly, two pages')
