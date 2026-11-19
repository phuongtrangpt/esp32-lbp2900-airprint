#!/usr/bin/env python3
"""C encoder compared byte-for-byte with independent MIT Python reference;
packet framing and full-size raster checked with upstream decoder. No hardware."""
import ctypes as C
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/reference'))
from lbp6030 import bandcodec,jobwriter,jobparser

def run(*a):subprocess.run(a,cwd=ROOT,check=True)
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp)
 common=['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Icomponents/lbp6030/include','-Icomponents/airprint/include']
 run(*common,'-shared','-fPIC','components/lbp6030/lbp6030.c','-o',str(tmp/'codec.so'))
 lib=C.CDLL(str(tmp/'codec.so'));enc=lib.lbp6030_encode
 enc.argtypes=[C.c_void_p,C.c_size_t,C.c_size_t,C.c_int,C.c_void_p,C.c_size_t];enc.restype=C.c_size_t
 rng=random.Random(6030)
 for stride in [8,40,620,2480,2552]:
  for rows in [1,2,8]:
   for kind in range(4):
    raw=(bytes(stride*rows) if kind==0 else bytes([0xee])*stride*rows if kind==1 else bytes(rng.randrange(256) for _ in range(stride*rows)) if kind==2 else (bytes(range(16))*(stride*rows//16+1))[:stride*rows])
    for last in [0,1]:
     dst=C.create_string_buffer(len(raw)*2+100)
     n=enc(raw,stride,rows,last,dst,len(dst));assert n
     got=dst.raw[:n];expected=bandcodec.BandEncoder(jobwriter.PARAM_4BPP).encode(raw,stride,rows,last)
     assert got==expected,(stride,rows,kind,last)
     assert bandcodec.decode_band(got,stride,rows,jobwriter.PARAM_4BPP)==raw
     assert enc(raw,stride,rows,last,dst,1)==0
 print('Codec: 120 cases including both end modes, byte-exact Python match, capacity rejection OK')
 run(*common,'-fsanitize=address,undefined','-fno-omit-frame-pointer','host/lbp6030_host.c','components/printers/canon_lbp6030.c','components/lbp6030/lbp6030.c','-o',str(tmp/'host'))
 import os
 os.environ['ASAN_OPTIONS']='detect_leaks=0'
 for paper in [0,1]:
  path=tmp/'job.bin';run(str(tmp/'host'),str(path),str(paper),'2');job=path.read_bytes()
  pk=list(jobparser.packets(job));assert sum(20+len(p['payload']) for p in pk)==len(job)
  assert all(len(p['payload'])<=4076 for p in pk)
  assert pk[-2]['payload']==b'\x07\xd6'+struct.pack('>I',len(job))
  pdl=jobparser.pdl_from_job(job);bands=jobparser.parse_bands(pdl)
  w,h=(5100,6600) if paper else (4958,7016);height=(h*2+2)//3;stride=((w*4+31)//32)*4
  cursor=0;page=0
  for b in bands:
   assert b['len']<32768 and b['y']==cursor and b['h']<=8
   raw=bandcodec.decode_band(b['stream'],stride,b['h'],b['param'])
   for row in range(b['h']):
    k=cursor+row;m=k//2
    fy=3*m+(1 if k%2 else 0);hy=3*m+(2 if k%2 else -1)
    for x in [0,1,8,150,w-2,w-1]:
     full=(x*13+fy*7+page*89)%256 if fy<h else 255
     half=(x*13+hy*7+page*89)%256 if 0<=hy<h else 255
     expected=((3*255-2*full-half)*14+382)//765
     got=(raw[row*stride+x//2]>>(0 if x%2 else 4))&15
     assert got==expected,(paper,k,x,got,expected)
   cursor+=b['h']
   assert b['flag']==(0 if cursor==height else 1)
   if cursor==height:cursor=0;page+=1
  assert page==2 and cursor==0
  # Added flushes are legal; no >64KiB between explicit empty packets.
  count=0
  for p in pk:
   if p['op']==0x1a:
    if len(p['payload'])==1:count=0
    else:count+=len(p['payload'])-1;assert count<=65536
  print(('Letter' if paper else 'A4')+': 2 pages decoded, raster locations / band limits / footer / flush boundaries OK')
 print('ASAN/UBSAN: page jobs and injected disconnect/cancel cases passed')
