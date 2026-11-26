# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise packaged worker, actual device monitoring and native HP conversion."""
import http.client
import json
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_offload import raster,ipp
from fake_esp import FakeESP

def main():
    exe=Path(sys.argv[1]).resolve()
    with FakeESP(http_port=18080) as peer:
        p=subprocess.Popen([str(exe),'--esp','127.0.0.1','--esp-status-port','18080'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        events=queue.Queue()
        def read_events():
            for line in p.stdout:
                print(line.rstrip(),flush=True);events.put(json.loads(line))
        reader=threading.Thread(target=read_events,daemon=True);reader.start()
        def wait_phase(phase):
            until=time.monotonic()+30
            while time.monotonic()<until:
                e=events.get(timeout=30)
                if e['phase']=='failed':raise AssertionError(e)
                if e['phase']==phase:return e
            raise AssertionError('Missing phase '+phase)
        try:
            wait_phase('ready')
            conn=http.client.HTTPConnection('127.0.0.1',8632,timeout=15)
            conn.request('POST','/ipp/print',ipp(doc=raster(2)),headers={'Content-Type':'application/ipp'})
            r=conn.getresponse();data=r.read();conn.close();assert r.status==200 and data[2:4]==b'\0\0'
            wait_phase('usb-delivered')
            assert len(peer.captured)==1 and not peer.errors,peer.errors
            assert peer.captured[0].endswith(b'\x1b%-12345X@PJL EOJ\n\x1b%-12345X')
            print('PACKAGED WORKER: device monitoring -> HTTP spool -> native HP -> simulated ESP passed')
        finally:
            p.stdin.write('stop\n');p.stdin.flush()
            try:p.wait(timeout=15)
            except subprocess.TimeoutExpired:p.kill();p.wait();raise
            reader.join(timeout=2);stderr=p.stderr.read();p.stdin.close();p.stdout.close();p.stderr.close()
            assert p.returncode==0,(p.returncode,stderr)

if __name__=='__main__':main()
