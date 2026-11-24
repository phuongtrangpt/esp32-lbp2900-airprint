#!/usr/bin/env python3
"""Six-page 300-dpi HP Send-Document across 9.5 MiB; native radio/USB are simulated."""
import http.client,json,os,socket,struct,subprocess,tempfile,time
from pathlib import Path
from test_capt_ipp import attribute
ROOT=Path(__file__).resolve().parents[1]
LIMIT=19*512*1024

def prefix(port,op,job=None):
 p=struct.pack('>BBHI',2,0,op,47)+b'\x01'
 for tag,name,value in [(0x47,'attributes-charset','utf-8'),(0x48,'attributes-natural-language','en'),(0x45,'printer-uri',f'ipp://localhost:{port}/ipp/print')]:p+=attribute(tag,name,value)
 if job is not None:p+=attribute(0x21,'job-id',struct.pack('>I',job))
 if op==6:p+=attribute(0x22,'last-document',b'\x01')+attribute(0x49,'document-format','image/urf')
 return p+b'\x03'

def request(port,op,job=None):
 c=http.client.HTTPConnection('127.0.0.1',port,timeout=120)
 c.request('POST','/ipp/print',prefix(port,op,job),{'Content-Type':'application/ipp'})
 r=c.getresponse();b=r.read();c.close();assert r.status==200
 return struct.unpack_from('>H',b,2)[0],b

def document():
 w,h=2480,3507
 header=bytearray(32);header[:4]=bytes([8,0,1,4]);struct.pack_into('>III',header,12,w,h,300)
 raw=bytes(0 if (x//16)%2 else 255 for x in range(w));line=bytearray(b'\0')
 for off in range(0,w,128):
  block=raw[off:off+128];line+=bytes([257-len(block)])+block
 page=bytes(header)+bytes(line)*700
 left=h-700
 while left:
  n=min(left,256);page+=bytes([n-1,128]);left-=n
 data=b'UNIRAST\0'+struct.pack('>I',6)+page*6
 assert len(data)>LIMIT and 12+len(page)*5<LIMIT<12+len(page)*6
 return data

def send(port,doc):
 code,b=request(port,5);assert code==0,hex(code)
 field=b'job-id\x00\x04';i=b.index(field)+len(field);job=struct.unpack_from('>I',b,i)[0]
 s=socket.create_connection(('127.0.0.1',port),timeout=120)
 s.sendall(b'POST /ipp/print HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/ipp\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n')
 # Separate IPP metadata means the 9.5-MiB cut refers exactly to raster bytes.
 for data in [prefix(port,6,job)]+[doc[i:i+512*1024] for i in range(0,len(doc),512*1024)]:
  size=f'{len(data):x};windows-style=1\r\n'.encode();s.sendall(size[:1]);s.sendall(size[1:])
  for i in range(0,len(data),8191):s.sendall(data[i:i+8191])
  s.sendall(b'\r\n')
 s.sendall(b'0\r\nFinal: yes\r\n\r\n')
 response=http.client.HTTPResponse(s);response.begin();b=response.read();s.close();assert response.status==200
 c=http.client.HTTPConnection('127.0.0.1',port,timeout=10);c.request('GET','/api/state');state=json.loads(c.getresponse().read());c.close()
 return struct.unpack_from('>H',b,2)[0],state['printer']['jobs'][0]

def main():
 doc=document()
 for direct in (False,True):
  with tempfile.TemporaryDirectory() as tmp:
   work=Path(tmp);(work/'out').mkdir();logfile=work/'server.log'
   with socket.socket() as listener:listener.bind(('127.0.0.1',0));port=listener.getsockname()[1]
   env=dict(os.environ,PRINTER_DRIVER='hp-p1006',ASAN_OPTIONS='detect_leaks=0',AIRPRINT_HOST_SLOW_US='1000')
   if direct:env['AIRPRINT_TEST_READ_AHEAD_FAIL']='1'
   with logfile.open('w') as log:
    server=subprocess.Popen([str(ROOT/'host/hp_ipp_host'),str(port)],cwd=work,env=env,stdout=log,stderr=log)
    try:
     for _ in range(200):
      assert server.poll() is None,logfile.read_text()
      try:
       with socket.create_connection(('127.0.0.1',port),.1):break
      except OSError:time.sleep(.05)
     for run in range(3):
      code,j=send(port,doc);assert code==0 and j['state']=='completed' and j['pages']==6,(code,j,logfile.read_text()[-3000:])
      print(('direct' if direct else 'read-ahead'),run+1,len(doc),'bytes: six pages completed',flush=True)
     code,j=send(port,doc[:LIMIT]);assert code==0x0411 and j['state']=='aborted' and j['pages']==5,(hex(code),j)
     print(('direct' if direct else 'read-ahead'),'intentional 9.5-MiB EOF reproduces five pages + page-six failure',flush=True)
     code,j=send(port,doc);assert code==0 and j['pages']==6
     text=logfile.read_text();assert ('IPP read-ahead unavailable' if direct else 'IPP read-ahead 105') in text,text[-3000:]
     assert 'ERROR: AddressSanitizer' not in text and 'runtime error:' not in text
     print('following full six-page job recovers, no sanitizer errors',flush=True)
    finally:server.terminate();server.wait(10)
if __name__=='__main__':main()
