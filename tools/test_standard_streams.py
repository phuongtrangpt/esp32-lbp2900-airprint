#!/usr/bin/env python3
"""Validate generated PCL raster rows and render PostScript with Ghostscript."""
import pathlib,re,subprocess,tempfile
from PIL import Image
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
    prefix=pathlib.Path(td)/'standard'
    subprocess.run([str(root/'host/standard_host'),str(prefix)],check=True)
    for paper,(pw,ph) in enumerate([(21000,29700),(21590,27940)]):
        for dpi in (300,600):
            w,h=pw*dpi//2540,ph*dpi//2540
            left=dpi//4; iw,ih=w-2*left,h-2*left; stride=(iw+7)//8
            data=pathlib.Path(f'{prefix}-0-{paper}-{dpi}.pcl').read_bytes()
            pos=0
            for page in range(2):
                pos=data.index(b'\x1b*r1A',pos)+5
                for row in range(ih):
                    match=re.match(rb'\x1b\*b(\d+)W',data[pos:]);assert match
                    n=int(match[1]);assert n==stride;pos+=match.end()
                    bits=data[pos:pos+n];pos+=n
                    expected=bytearray(stride)
                    if h//3<row+left<2*h//3:
                        for x in range(iw):
                            if w//3<x+left<2*w//3:expected[x//8]|=0x80>>(x%8)
                    assert bits==expected,(paper,dpi,page,row)
                assert data[pos:pos+5]==b'\x1b*rB\x0c';pos+=5
            ps=pathlib.Path(f'{prefix}-1-{paper}-{dpi}.ps')
            subprocess.run(['gs','-q','-dSAFER','-dBATCH','-dNOPAUSE','-sDEVICE=pbmraw','-r72',
                            f'-sOutputFile={td}/render-%d.pbm',str(ps)],check=True)
            for page in (1,2):
                im=Image.open(f'{td}/render-{page}.pbm'); ww,hh=im.size
                assert abs(ww-pw*72/2540)<2 and abs(hh-ph*72/2540)<2
                assert im.getpixel((ww//2,hh//2))==0
                assert im.getpixel((ww//4,hh//2))!=0
            print(f'paper {paper}, {dpi}dpi: every PCL raster pixel verified; 2 PS pages rendered, dimensions and polarity verified')
