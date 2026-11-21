#!/usr/bin/env python3
"""Exercise the actual IPP -> URF -> CAPT pipeline with a simulated printer."""
import http.client
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent

def attribute(tag, name, value):
    name = name.encode()
    if isinstance(value, str): value = value.encode()
    return bytes([tag]) + struct.pack('>H', len(name)) + name + struct.pack('>H', len(value)) + value

def ipp(port, op, document=b'', dpi=600):
    body = struct.pack('>BBHI', 2, 0, op, 7) + b'\x01'
    body += attribute(0x47, 'attributes-charset', 'utf-8')
    body += attribute(0x48, 'attributes-natural-language', 'en')
    body += attribute(0x45, 'printer-uri', f'ipp://localhost:{port}/ipp/print')
    body += attribute(0x42, 'requesting-user-name', 'test')
    if op in (2, 4):
        body += attribute(0x49, 'document-format', 'image/urf')
        body += b'\x02' + attribute(0x32, 'printer-resolution', struct.pack('>IIB', dpi, dpi, 3))
    body += b'\x03' + document
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=90)
    conn.request('POST', '/ipp/print', body, {'Content-Type': 'application/ipp'})
    response = conn.getresponse()
    data = response.read()
    assert response.status == 200, response.status
    assert data[:2] == b'\x02\x00'
    status = struct.unpack('>H', data[2:4])[0]
    conn.close()
    return status, data

def white_urf(pages, dpi=600, width=4961, height=7016):
    data = b'UNIRAST\0' + struct.pack('>I', pages)
    for _ in range(pages):
        header = bytearray(32)
        header[:4] = bytes([8, 0, 1, 4])
        struct.pack_into('>III', header, 12, width, height, dpi)
        data += header
        left = height
        while left:
            rows = min(left, 256)
            data += bytes([rows - 1, 128])
            left -= rows
    return data

def main():
    pages = int(os.environ.get("TEST_CAPT_PAGES", "2"))
    assert 1 <= pages <= 1000
    # Reserve a free loopback port; the subprocess binds immediately afterwards.
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    env = dict(os.environ, PRINTER_DRIVER='canon-lbp2900', ASAN_OPTIONS='detect_leaks=0')
    (ROOT / 'host/out').mkdir(exist_ok=True)
    with (ROOT / 'host/out/capt-ipp.log').open('w') as log:
        server = subprocess.Popen([str(ROOT / 'host/airprint_host'), str(port)], cwd=ROOT / 'host', env=env,
                                  stdout=log, stderr=log)
        try:
            for _ in range(100):
                if server.poll() is not None: raise RuntimeError('server exited')
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.2): break
                except OSError: time.sleep(.1)
            else: raise RuntimeError('server did not start')
            status, data = ipp(port, 0x0b)
            assert status == 0
            assert b'Canon LBP2900' in data and b'image/urf' in data
            status, _ = ipp(port, 4, dpi=300)
            assert status == 0, '300 dpi must be accepted'
            status, _ = ipp(port, 4, dpi=150)
            assert status >= 0x400, '150 dpi must be refused'
            status, _ = ipp(port, 2, white_urf(2, dpi=300, width=2480, height=3508), dpi=300)
            assert status == 0, hex(status)
            status, _ = ipp(port, 2, white_urf(pages, dpi=300, width=2480, height=3508), dpi=300)
            assert status == 0, hex(status)
            # Clients with cached 600 dpi capabilities must still be able to print.
            # 600 dpi is accepted but is NOT advertised as an available resolution.
            status, _ = ipp(port, 4, dpi=600)
            assert status == 0, 'legacy 600 dpi job attributes must be accepted'
            status, _ = ipp(port, 2, white_urf(1), dpi=600)
            assert status == 0, 'legacy 600 dpi raster must still print'
            conn = http.client.HTTPConnection('127.0.0.1', port)
            conn.request('GET', '/api/state')
            result = json.loads(conn.getresponse().read())
            assert result['printer']['jobs'][0]['state'] == 'completed', result
            assert result['printer']['jobs'][0]['pages'] == pages, result
            assert not result['scan']['scanner'], result
            # A malformed/unsupported raster aborts only this job; a following
            # valid job must still complete, with the simulated printer reset.
            status, _ = ipp(port, 2, b'not-a-raster')
            assert status >= 0x400, hex(status)
            status, _ = ipp(port, 2, white_urf(1, dpi=300, width=2480, height=3508), dpi=300)
            assert status == 0, hex(status)
            print(f'IPP CAPT: PASS: capabilities, 300 dpi two-page draft and unsupported 150 dpi with 600 dpi compatibility, {pages}-page 300 dpi URF job, completed state, scanner disabled, malformed raster and next-job recovery')
        finally:
            server.terminate()
            try: server.wait(timeout=5)
            except subprocess.TimeoutExpired: server.kill(); server.wait()

if __name__ == '__main__': main()
