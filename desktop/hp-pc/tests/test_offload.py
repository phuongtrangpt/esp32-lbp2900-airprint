# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import http.client
import io
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import zlib
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'desktop/hp-pc/worker'))
from transport import HEADER, FRAME, response, send_job, hello
from ipp_codec import attr, decode, integer, reply, collection
from worker import Service, Server
BUILD = Path(os.environ.get('HP_PC_BUILD', '/tmp/hp-pc-build'))
RENDER = BUILD / ('hp-render.exe' if os.name == 'nt' else 'hp-render')
BRIDGE = BUILD / 'bridge-host'


def raster(pages=1, rgb=False):
    # A6 300 dpi; nonblank rows exercise mono packing/JBIG, long repeats too.
    w, h = 1240, 1748
    data = bytearray(b'RaS2')
    for page in range(pages):
        header = bytearray(1796)
        for pos, v in [(276,300),(280,300),(372,w),(376,h),(384,8),(388,24 if rgb else 8),(392,w*(3 if rgb else 1)),(396,0),(400,19 if rgb else 18),(420,3 if rgb else 1)]:
            struct.pack_into('!I', header, pos, v)
        data.extend(header)
        for y in range(0, h, 256):
            n = min(256,h-y)
            data.append(n-1)
            # A solid dark stripe followed by white remainder.
            data.extend(bytes([99]) + (bytes([page*21%128])* (3 if rgb else 1)))
            data.append(128)
    return bytes(data)


def ipp(op=2, req=7, attrs=b'', doc=b''):
    return struct.pack('!2sHI',b'\x02\x00',op,req)+b'\x01'+attr(0x47,'attributes-charset','utf-8')+attr(0x48,'attributes-natural-language','en')+attrs+b'\x03'+doc


@contextlib.contextmanager
def bridge(path, mode=0):
    p=subprocess.Popen([str(BRIDGE),str(path),str(mode)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    line=p.stdout.readline().strip()
    if not line:
        raise RuntimeError(p.stderr.read())
    try:
        yield int(line)
    finally:
        p.terminate();p.wait(timeout=5)
        err=p.stderr.read()
        if 'ERROR: AddressSanitizer' in err or 'runtime error:' in err:
            raise AssertionError(err)
        p.stdout.close();p.stderr.close()


class Conversion(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):
        self.temp.cleanup()
    def render(self,data):
        inp=self.root/'in.ras';out=self.root/'out.xqx';inp.write_bytes(data)
        p=subprocess.run([str(RENDER),str(inp),str(out)],capture_output=True)
        return p,out
    def test_six_pages_complete(self):
        p,out=self.render(raster(6));self.assertEqual(p.returncode,0,p.stderr)
        self.assertEqual(json.loads(p.stdout)['pages'],6)
        data=out.read_bytes();self.assertTrue(data.startswith(b'\x1b%-12345X@PJL JOB'))
        self.assertTrue(data.endswith(b'\x1b%-12345X@PJL EOJ\n\x1b%-12345X'))
    def test_truncated_sixth_page_produces_no_output(self):
        p,out=self.render(raster(6)[:-3]);self.assertNotEqual(p.returncode,0)
        self.assertFalse(out.exists());self.assertFalse(Path(str(out)+'.tmp').exists())
    def test_truncated_header_and_trailing_garbage(self):
        for data in (raster()[:100],raster()+b'x',b'bad',b'RaS2'):
            p,out=self.render(data);self.assertNotEqual(p.returncode,0);self.assertFalse(out.exists())
    def test_mixed_resolution_refused(self):
        data=bytearray(raster(2));offset=4+(len(data)-4)//2
        struct.pack_into('!I',data,offset+276,600);struct.pack_into('!I',data,offset+280,600)
        p,out=self.render(data);self.assertNotEqual(p.returncode,0);self.assertFalse(out.exists())
    def test_urf_white_matches_pwg_white(self):
        w,h=1240,1748
        urf=bytearray(b'UNIRAST\0'+struct.pack('!I',2))
        pwg=bytearray(b'RaS2')
        for _ in range(2):
            ph=bytearray(32);ph[:4]=bytes([8,0,1,4]);struct.pack_into('!III',ph,12,w,h,300);urf.extend(ph)
            ph=bytearray(1796)
            for pos,v in [(276,300),(280,300),(372,w),(376,h),(384,8),(388,8),(392,w),(400,18)]:struct.pack_into('!I',ph,pos,v)
            pwg.extend(ph)
            for y in range(0,h,256):
                block=bytes([min(256,h-y)-1,128]);urf.extend(block);pwg.extend(block)
        p,out=self.render(urf);self.assertEqual(p.returncode,0,p.stderr);expected=out.read_bytes();out.unlink()
        p,out=self.render(pwg);self.assertEqual(p.returncode,0,p.stderr);self.assertEqual(out.read_bytes(),expected);out.unlink()
        p,out=self.render(urf[:-1]);self.assertNotEqual(p.returncode,0);self.assertFalse(out.exists())
        # A declared page count that disagrees with actual pages is refused.
        struct.pack_into('!I',urf,8,1)
        p,out=self.render(urf);self.assertNotEqual(p.returncode,0);self.assertFalse(out.exists())
    def test_rgb(self):
        p,_=self.render(raster(rgb=True));self.assertEqual(p.returncode,0,p.stderr)


@unittest.skipIf(os.name=='nt','POSIX fake USB runner; Windows converter/IPP tests run separately')
class Transport(Conversion):
    def test_transport_byte_exact(self):
        p,out=self.render(raster(2));self.assertEqual(p.returncode,0)
        sink=self.root/'usb'
        with bridge(sink) as port:
            self.assertTrue(hello('127.0.0.1',port))
            send_job('127.0.0.1',out,threading.Event(),lambda *args:None,port=port)
        self.assertEqual(out.read_bytes(),sink.read_bytes())
    def test_busy_and_wrong_model(self):
        for mode in (1,2):
            sink=self.root/'usb'
            with bridge(sink,mode) as port:
                with self.assertRaises(ConnectionError):hello('127.0.0.1',port)
            self.assertEqual(sink.read_bytes(),b'')
    def test_crc_sequence_and_fragmentation(self):
        p,out=self.render(raster());self.assertEqual(p.returncode,0);data=out.read_bytes()
        sink=self.root/'usb'
        with bridge(sink) as port:
            for i,kind in enumerate(('crc','seq','preamble','valid')):
                with socket.create_connection(('127.0.0.1',port)) as s:
                    header=HEADER.pack(b'VPHPO01\n',2,len(data),zlib.crc32(data),100+i)
                    for byte in header:s.sendall(bytes([byte]))
                    self.assertEqual(response(s)[0],0)
                    block=data[:4096];seq=1 if kind=='seq' else 0
                    if kind=='preamble':block=b'X'+block[1:]
                    crc=zlib.crc32(block)^(1 if kind=='crc' else 0)
                    s.sendall(FRAME.pack(seq,len(block),crc)+block)
                    result=response(s)
                    self.assertEqual(result[0],1 if kind=='valid' else 104 if kind=='crc' else 100)
            # An ACKed partial upload is recorded as unknown, never replays.
            with socket.create_connection(('127.0.0.1',port)) as s:
                s.sendall(HEADER.pack(b'VPHPO01\n',3,0,0,103));self.assertEqual(response(s)[0],108)
        self.assertEqual(sink.read_bytes(),data[:4096])
    def test_duplicate_job_and_lost_final_ack(self):
        p,out=self.render(raster());self.assertEqual(p.returncode,0);data=out.read_bytes();sink=self.root/'usb';job=777
        with bridge(sink) as port:
            with socket.create_connection(('127.0.0.1',port)) as s:
                h=HEADER.pack(b'VPHPO01\n',2,len(data),zlib.crc32(data),job);s.sendall(h);self.assertEqual(response(s)[0],0)
                seq=0
                for off in range(0,len(data),4096):
                    block=data[off:off+4096];s.sendall(FRAME.pack(seq,len(block),zlib.crc32(block))+block);self.assertEqual(response(s)[0],1);seq+=1
                s.sendall(FRAME.pack(seq,0,0))
                # Deliberately don't read final ACK; resolve with QUERY only.
            with socket.create_connection(('127.0.0.1',port)) as s:
                s.sendall(HEADER.pack(b'VPHPO01\n',3,0,0,job));self.assertEqual(response(s)[0],2)
            with socket.create_connection(('127.0.0.1',port)) as s:
                s.sendall(h);self.assertEqual(response(s)[0],2)
        self.assertEqual(sink.read_bytes(),data)


class Spooler(unittest.TestCase):
    def setUp(self):
        self.service=Service('127.0.0.1',RENDER)
        self.server=Server(('127.0.0.1',0),self.service)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.thread.join();self.service.stop()
    def post(self,body,chunked=False):
        conn=http.client.HTTPConnection('127.0.0.1',self.server.server_port,timeout=10)
        payload=(body[i:i+37] for i in range(0,len(body),37)) if chunked else body
        conn.request('POST','/ipp/print',payload,headers={'Content-Type':'application/ipp'},encode_chunked=chunked)
        response=conn.getresponse();data=response.read();status=response.status;conn.close();self.assertEqual(status,200)
        return struct.unpack_from('!H',data,2)[0],data
    def wait_job(self,j):
        until=time.monotonic()+15
        while j['state']<7 and time.monotonic()<until:time.sleep(.01)
        self.assertGreaterEqual(j['state'],7)
    def test_attributes_and_validate(self):
        status,data=self.post(ipp(11));self.assertEqual(status,0)
        _,_,a=decode(io.BytesIO(data));self.assertIn('image/pwg-raster',[v.decode() for _,v in a['document-format-supported']])
        self.assertEqual(self.post(ipp(4,attrs=attr(0x21,'copies',2)))[0],0x040b)
        self.assertEqual(self.post(ipp(4,attrs=attr(0x49,'document-format','application/pdf')))[0],0x040a)
    def test_bad_last_page_never_contacts_esp(self):
        with patch('worker.send_job') as send:
            status,_=self.post(ipp(doc=raster(6)[:-3]),chunked=True);self.assertEqual(status,0)
            j=next(iter(self.service.jobs.values()));self.wait_job(j)
            self.assertEqual(j['state'],8);send.assert_not_called()
            self.assertFalse(list(self.service.root.glob('*.xqx')))
    def test_create_send_and_no_replay(self):
        with patch('worker.send_job') as send:
            status,data=self.post(ipp(5));self.assertEqual(status,0)
            _,_,a=decode(io.BytesIO(data));job=integer(a,'job-id')
            attrs=attr(0x21,'job-id',job)+attr(0x22,'last-document',True)
            status,_=self.post(ipp(6,attrs=attrs,doc=raster()),chunked=True);self.assertEqual(status,0)
            self.wait_job(self.service.jobs[job]);self.assertEqual(self.service.jobs[job]['state'],9)
            self.assertEqual(send.call_count,1)
            self.assertEqual(self.post(ipp(6,req=8,attrs=attrs,doc=raster()))[0],0x0405)
    def test_media_collection_and_dedup(self):
        media = collection('media-col', [('media-size',0x34,[('x-dimension',0x21,21000),('y-dimension',0x21,29700)])])
        self.assertEqual(self.post(ipp(4,attrs=media))[0],0)
        with patch('worker.send_job') as send:
            body = ipp(doc=raster())
            self.assertEqual(self.post(body)[0],0)
            self.assertEqual(self.post(body)[0],0)
            j=next(iter(self.service.jobs.values()));self.wait_job(j)
            self.assertEqual(len(self.service.jobs),1)
            self.assertEqual(send.call_count,1)
    def test_cancel_held_job(self):
        _,data=self.post(ipp(5));_,_,a=decode(io.BytesIO(data));job=integer(a,'job-id')
        self.assertEqual(self.post(ipp(8,attrs=attr(0x21,'job-id',job)))[0],0)
        self.assertEqual(self.service.jobs[job]['state'],7)
    def test_truncated_http_no_job(self):
        with socket.create_connection(('127.0.0.1',self.server.server_port)) as s:
            body=ipp(doc=raster());s.sendall(b'POST /ipp/print HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/ipp\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body[:-5]);s.shutdown(socket.SHUT_WR)
            self.assertIn(b'400',s.recv(4096))
        self.assertEqual(len(self.service.jobs),0)


if __name__=='__main__':unittest.main()
