# SPDX-License-Identifier: GPL-3.0-or-later
"""Android Java transport against the actual portable ESP protocol, with fake USB."""
import os,sys,subprocess,tempfile,socket,threading,struct,zlib,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'desktop/hp-pc/tests'))
from test_offload import bridge,raster
BUILD=Path('/tmp/hp-android-test')
JAVA=ROOT/'android/hp-p1006/app/src/main/java/vn/io/vietphuong/printconnect/Transport.java'
class Protocol(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  BUILD.mkdir(exist_ok=True)
  subprocess.run(['javac','-d',str(BUILD),str(JAVA),str(Path(__file__).with_name('TransportHarness.java'))],check=True)
  # The actual ESP parser requires valid PJL + XQX, not arbitrary TCP bytes.
  data=bytearray(raster()[:1800])
  for y in range(1748):
   data.append(0);row=os.urandom(1240)
   for x in range(0,1240,128):
    block=row[x:x+128];data.append(257-len(block));data+=block
  source=BUILD/'fixture.ras';source.write_bytes(data);output=BUILD/'fixture.xqx'
  subprocess.run(['/tmp/hp-pc-build/hp-render',str(source),str(output)],check=True,capture_output=True)
  cls.fixture=output.read_bytes()
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.path=Path(self.tmp.name)/'job';self.path.write_bytes(self.fixture);self.sink=Path(self.tmp.name)/'usb'
 def tearDown(self):self.tmp.cleanup()
 def java(self,port,op='send'):
  return subprocess.run(['java','-cp',str(BUILD),'vn.io.vietphuong.printconnect.TransportHarness',op,str(port),str(self.path)],capture_output=True,timeout=35)
 def test_byte_exact_multiple_frames(self):
  with bridge(self.sink) as port:r=self.java(port)
  self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(self.sink.read_bytes(),self.path.read_bytes())
 def test_busy_and_wrong_model_send_no_document(self):
  for mode in [1,2]:
   with bridge(self.sink,mode) as port:r=self.java(port)
   self.assertNotEqual(r.returncode,0);self.assertEqual(self.sink.read_bytes(),b'')
 def test_usb_error_is_not_replayed(self):
  with bridge(self.sink,3) as port:r=self.java(port)
  self.assertNotEqual(r.returncode,0);self.assertIn('Không tự gửi lại'.encode(),r.stderr)
 def test_backpressure_thirteen_seconds(self):
  with bridge(self.sink,4) as port:r=self.java(port)
  self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(self.sink.read_bytes(),self.path.read_bytes())
 def test_cancel_after_first_ack_no_replay(self):
  with bridge(self.sink) as port:r=self.java(port,'cancel')
  self.assertNotEqual(r.returncode,0);self.assertEqual(self.sink.read_bytes(),self.path.read_bytes()[:4096])
 def test_lost_final_ack_queries_only(self):
  server=socket.socket();server.bind(('127.0.0.1',0));server.listen();errors=[];seen=[]
  def exact(s,n):
   b=b''
   while len(b)<n:
    c=s.recv(n-len(b));assert c;b+=c
   return b
  def reply(s,status,seq,size):s.sendall(struct.pack('!8sIIQ',b'VPHPA01\n',status,seq,size))
  def run():
   try:
    with server.accept()[0] as s:
     magic,op,total,crc,job=struct.unpack('!8sIQIQ',exact(s,32));seen.append(op);reply(s,0,0,0);data=b''
     while True:
      seq,n,c=struct.unpack('!III',exact(s,12))
      if not n:break
      b=exact(s,n);assert zlib.crc32(b)==c;data+=b;reply(s,1,seq,len(data))
     assert data==self.path.read_bytes() and zlib.crc32(data)==crc
    with server.accept()[0] as s:
     m,op,_,_,ident=struct.unpack('!8sIQIQ',exact(s,32));seen.append(op);assert ident==job;reply(s,2,0,total)
   except Exception as e:errors.append(repr(e))
   finally:server.close()
  t=threading.Thread(target=run,daemon=True);t.start();r=self.java(server.getsockname()[1]);t.join(5)
  self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(errors,[]);self.assertEqual(seen,[2,3])
 def test_android_native_matches_windows_encoder(self):
  native=self.sink;subprocess.run([str(BUILD/'encoder-host'),str(native)],check=True)
  w,h=1240,1748;data=bytearray(b'RaS2')
  for page in range(2):
   header=bytearray(1796)
   for pos,value in [(276,300),(280,300),(372,w),(376,h),(384,8),(388,8),(392,w),(400,18)]:struct.pack_into('!I',header,pos,value)
   data+=header
   for y in range(h):
    data.append(0);row=bytes((x+y)%256 if x<160 and y<160 else 255 for x in range(w))
    for x in range(0,w,128):
     part=row[x:x+128];data.append(257-len(part));data+=part
  self.path.write_bytes(data);out=self.path.with_suffix('.xqx');subprocess.run(['/tmp/hp-pc-build/hp-render',str(self.path),str(out)],check=True,capture_output=True)
  self.assertEqual(native.read_bytes(),out.read_bytes())
if __name__=='__main__':unittest.main()
