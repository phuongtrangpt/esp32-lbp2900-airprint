#!/usr/bin/env python3
"""End-to-end IPP socket stress tests using the native Canon CAPT simulator.

A large body is intentionally appended after valid URF pages to exercise the
HTTP upload/drain path at the observed 512 KiB boundary. It does NOT model a
512 KiB raster decoding workload; the existing CAPT tests cover raster pages.
"""
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import time

from test_capt_ipp import ROOT, attribute, ipp, white_urf
import struct


def ipp_payload(port, document):
    body = struct.pack('>BBHI', 2, 0, 2, 91) + b'\x01'
    body += attribute(0x47, 'attributes-charset', 'utf-8')
    body += attribute(0x48, 'attributes-natural-language', 'en')
    body += attribute(0x45, 'printer-uri', f'ipp://localhost:{port}/ipp/print')
    body += attribute(0x49, 'document-format', 'image/urf')
    body += b'\x03'
    return body + document


def send_slow(port, data):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=120)
    conn.putrequest('POST', '/ipp/print')
    conn.putheader('Content-Type', 'application/ipp')
    conn.putheader('Content-Length', str(len(data)))
    conn.endheaders()
    for pos in range(0, len(data), 4096):
        conn.send(data[pos:pos + 4096])
        if pos % 65536 == 0:
            time.sleep(0.03)
    response = conn.getresponse()
    payload = response.read()
    status = struct.unpack('>H', payload[2:4])[0]
    assert response.status == 200 and status == 0, (response.status, hex(status))
    conn.close()


def disconnect_at_boundary(port, payload):
    # Declare a longer body, then close the socket at exactly 512 KiB.
    # This must never crash/hang the server or poison the next print job.
    with socket.create_connection(('127.0.0.1', port), timeout=10) as sock:
        header = (f'POST /ipp/print HTTP/1.1\r\nHost: localhost\r\n'
                  f'Content-Type: application/ipp\r\n'
                  f'Content-Length: {len(payload) + 32768}\r\n'
                  'Connection: close\r\n\r\n').encode()
        sock.sendall(header)
        sock.sendall(payload[:524288])


def main():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    env = dict(os.environ, PRINTER_DRIVER='canon-lbp2900', ASAN_OPTIONS='detect_leaks=0')
    (ROOT / 'host/out').mkdir(exist_ok=True)
    with (ROOT / 'host/out/capt-ipp-transport.log').open('w') as log:
        server = subprocess.Popen([str(ROOT / 'host/airprint_host'), str(port)],
                                  cwd=ROOT / 'host', env=env, stdout=log, stderr=log)
        try:
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError('simulated CAPT server exited')
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.2):
                        break
                except OSError:
                    time.sleep(.1)
            else:
                raise RuntimeError('simulated CAPT server not listening')

            # 1 MiB upload crosses 512 KiB and is fragmented in ~4 KiB blocks.
            # The trailing bytes exercise HTTP body draining after raster parsing.
            valid = white_urf(4, dpi=300, width=2480, height=3508)
            payload = ipp_payload(port, valid + bytes(1024 * 1024))
            send_slow(port, payload)

            # The same boundary with a hard client disconnect.
            disconnect_at_boundary(port, payload)
            time.sleep(0.3)
            assert server.poll() is None, 'client disconnect crashed print server'

            # Server should accept another job after the interrupted request.
            status, _ = ipp(port, 2, white_urf(2, dpi=300, width=2480, height=3508), dpi=300)
            assert status == 0, f'next job failed after disconnect: {status:#06x}'
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            connection.request('GET', '/api/state')
            state = json.loads(connection.getresponse().read())
            connection.close()
            assert state['printer']['jobs'][0]['state'] == 'completed', state
            print('PASS: 1 MiB slow IPP upload, 512 KiB client disconnect, '
                  'CAPT simulator and next-job recovery')
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == '__main__':
    main()
