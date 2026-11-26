import sys,threading,http.client,time,contextlib,io,os
from pathlib import Path
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[3]
sys.path.insert(0,str(root/'desktop/hp-pc/worker'))
from worker import Service,Server
from ipp_codec import attr
class Receiver(Service):
 def process(self):
  while not self.shutdown.is_set():
   try:j=self.pending.get(timeout=.1)
   except Exception:continue
   j['state']=9;self.pending.task_done()
with contextlib.redirect_stdout(io.StringIO()):
 service=Receiver('127.0.0.1','unused');server=Server(('127.0.0.1',0),service);t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
 body=b'\2\0\0\2\0\0\0\1\1'+attr(0x47,'attributes-charset','utf-8')+attr(0x48,'attributes-natural-language','en')+b'\3'+b'x'*(64*1024*1024)
 times=[]
 try:
  for n in range(3):
   c=http.client.HTTPConnection('127.0.0.1',server.server_port,timeout=30);start=time.perf_counter();c.request('POST','/ipp/print',body,headers={'Content-Type':'application/ipp'});r=c.getresponse();data=r.read();assert r.status==200 and data[2:4]==b'\0\0';times.append(round((time.perf_counter()-start)*1000,1));c.close()
 finally:server.shutdown();server.server_close();service.stop();t.join()
print('64 MiB receive + handoff milliseconds (3 runs; renderer/USB excluded):',times)
