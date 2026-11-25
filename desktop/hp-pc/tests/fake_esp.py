# SPDX-License-Identifier: GPL-3.0-or-later
import json
import socket
import threading
import zlib
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from transport import HEADER,FRAME,REPLY,exact
class FakeESP:
    def __init__(self,http_port=80,pc_port=9101):
        self.connected=True;self.reachable=True;self.busy=False;self.reject_http=False;self.captured=[];self.errors=[];self.stop=threading.Event();self.last=(0,0)
        owner=self
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*a):pass
            def do_GET(self):
                if not owner.reachable:self.close_connection=True;return
                if owner.reject_http:self.send_error(503);return
                body=json.dumps({'printer':{'connected':owner.connected,'model':'HP LaserJet P1006','state':'processing' if owner.busy else 'idle','reasons':[]}}).encode()
                self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        self.http=ThreadingHTTPServer(('127.0.0.1',http_port),Handler);self.http.daemon_threads=True;self.http_port=self.http.server_port
        self.tcp=socket.socket();self.tcp.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);self.tcp.bind(('127.0.0.1',pc_port));self.pc_port=self.tcp.getsockname()[1];self.tcp.listen(5);self.tcp.settimeout(.2)
    def start(self):
        self.ht=threading.Thread(target=self.http.serve_forever,daemon=True);self.ht.start()
        self.pt=threading.Thread(target=self.serve,daemon=True);self.pt.start();return self
    def serve(self):
        while not self.stop.is_set():
            try:s,_=self.tcp.accept()
            except socket.timeout:continue
            except OSError:break
            try:
                with s:
                    s.settimeout(5)
                    if not self.reachable:continue
                    magic,op,total,crc,job=HEADER.unpack(exact(s,32));assert magic==b'VPHPO01\n'
                    ready=102 if not self.connected else 101 if self.busy else 0
                    if op==1:s.sendall(REPLY.pack(b'VPHPA01\n',ready,0,0));continue
                    if op==3:s.sendall(REPLY.pack(b'VPHPA01\n',2 if job==self.last[0] else 109,0,self.last[1]));continue
                    assert op==2
                    s.sendall(REPLY.pack(b'VPHPA01\n',ready,0,0))
                    if ready:continue
                    data=bytearray();seq=0
                    while True:
                        i,n,c=FRAME.unpack(exact(s,12));assert i==seq
                        if not n:
                            assert len(data)==total and zlib.crc32(data)==crc and c==0
                            self.last=(job,len(data));self.captured.append(bytes(data));s.sendall(REPLY.pack(b'VPHPA01\n',2,seq,len(data)));break
                        block=exact(s,n);assert zlib.crc32(block)==c;data.extend(block)
                        s.sendall(REPLY.pack(b'VPHPA01\n',1,seq,len(data)));seq+=1
            except Exception as e:self.errors.append(repr(e))
    def close(self):
        self.stop.set();self.tcp.close();self.http.shutdown();self.http.server_close();self.ht.join(timeout=2);self.pt.join(timeout=2)
    def __enter__(self):return self.start()
    def __exit__(self,*a):self.close()
