# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the packaged worker and native encoder through a simulated ESP peer."""
import http.client
import json
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import threading
import time
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_offload import raster, ipp
from transport import HEADER, FRAME, REPLY, exact


def main():
    exe=Path(sys.argv[1]).resolve()
    listener=socket.socket();listener.bind(('127.0.0.1',9101));listener.listen(1);listener.settimeout(30)
    captured=[];errors=[]
    def peer():
        try:
            with listener.accept()[0] as s:
                s.settimeout(20)
                magic,op,total,crc,job=HEADER.unpack(exact(s,32))
                assert magic==b'VPHPO01\n' and op==2 and job
                s.sendall(REPLY.pack(b'VPHPA01\n',0,0,0))
                data=bytearray();seq=0
                while True:
                    i,n,c=FRAME.unpack(exact(s,12));assert i==seq
                    if not n:
                        assert len(data)==total and zlib.crc32(data)==crc and c==0
                        s.sendall(REPLY.pack(b'VPHPA01\n',2,seq,len(data)));break
                    block=exact(s,n);assert zlib.crc32(block)==c;data.extend(block)
                    s.sendall(REPLY.pack(b'VPHPA01\n',1,seq,len(data)));seq+=1
                captured.append(bytes(data))
        except Exception as e:
            errors.append(repr(e))
        finally:
            listener.close()
    t=threading.Thread(target=peer,daemon=True);t.start()
    p=subprocess.Popen([str(exe),'--esp','127.0.0.1'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    events=queue.Queue()
    def read_events():
        for line in p.stdout:
            print(line.rstrip(),flush=True)
            events.put(json.loads(line))
    reader=threading.Thread(target=read_events,daemon=True);reader.start()
    try:
        e=events.get(timeout=30);assert e['phase']=='ready',e
        conn=http.client.HTTPConnection('127.0.0.1',8632,timeout=15)
        body=ipp(doc=raster(2))
        conn.request('POST','/ipp/print',body,headers={'Content-Type':'application/ipp'})
        r=conn.getresponse();response=r.read();conn.close()
        assert r.status==200 and response[2:4]==b'\0\0'
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            e=events.get(timeout=30)
            if e['phase']=='failed':raise AssertionError(e)
            if e['phase']=='usb-delivered':break
        else:raise AssertionError('No USB completion')
        t.join(timeout=5);assert not errors,errors
        assert len(captured)==1 and captured[0].startswith(b'\x1b%-12345X@PJL JOB')
        assert captured[0].endswith(b'\x1b%-12345X@PJL EOJ\n\x1b%-12345X')
        print('PACKAGED WORKER: HTTP -> disk spool -> native HP JBIG/XQX -> simulated ESP passed')
    finally:
        p.stdin.write('stop\n');p.stdin.flush()
        try:p.wait(timeout=15)
        except subprocess.TimeoutExpired:p.kill();p.wait();raise
        reader.join(timeout=2)
        stderr=p.stderr.read();p.stdin.close();p.stdout.close();p.stderr.close()
        assert p.returncode==0,(p.returncode,stderr)


if __name__=='__main__':main()
