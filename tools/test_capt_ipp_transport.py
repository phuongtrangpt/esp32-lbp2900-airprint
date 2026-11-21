#!/usr/bin/env python3
"""Exercise real raster and HTTP framing through the native CAPT simulator."""
import http.client
import json
import os
import socket
import struct
import subprocess
import time

from test_capt_ipp import ROOT, attribute, ipp, white_urf


def ipp_payload(port, document, document_format='image/urf'):
    body = struct.pack('>BBHI', 2, 0, 2, 91) + b'\x01'
    body += attribute(0x47, 'attributes-charset', 'utf-8')
    body += attribute(0x48, 'attributes-natural-language', 'en')
    body += attribute(0x45, 'printer-uri', f'ipp://localhost:{port}/ipp/print')
    body += attribute(0x49, 'document-format', document_format)
    body += b'\x03'
    return body + document


def pwg_page(dpi, dense=False):
    width, height = (2480, 3508) if dpi == 300 else (4961, 7016)
    header = bytearray(1796)
    struct.pack_into('>II', header, 276, dpi, dpi)
    struct.pack_into('>II', header, 372, width, height)
    struct.pack_into('>III', header, 384, 8, 8, width)
    struct.pack_into('>I', header, 400, 18)  # sGray
    if not dense:
        groups, tail = divmod(height, 256)
        lines = bytes([255, 128]) * groups
        if tail:
            lines += bytes([tail - 1, 128])
        return bytes(header) + lines

    # Alternating gray literal pixels on every line, then compressed white.
    # No filler and no repeated scanlines. At 300 dpi this is >1 MiB per page.
    pixel_packets = bytes([129]) + bytes([40, 210]) * 64
    row = b'\x00' + pixel_packets * 3 + b'\x80'
    return bytes(header) + row * (height - 1) + b'\x00\x80'


def send_job(port, data, *, chunked=False, cut=None, slow=False,
             finish_chunks=True, during_upload=None):
    with socket.create_connection(('127.0.0.1', port), timeout=30) as sock:
        framing = ('Transfer-Encoding: chunked' if chunked else
                   f'Content-Length: {len(data)}')
        header = (f'POST /ipp/print HTTP/1.1\r\nHost: localhost\r\n'
                  f'Content-Type: application/ipp\r\n{framing}\r\n'
                  'Connection: close\r\n\r\n').encode()
        sock.sendall(header)
        sent = data if cut is None else data[:cut]
        for pos in range(0, len(sent), 4096):
            block = sent[pos:pos + 4096]
            if chunked:
                block = f'{len(block):x}\r\n'.encode() + block + b'\r\n'
            sock.sendall(block)
            if during_upload and pos == 65536:
                during_upload()
            if slow and pos % 65536 == 0:
                time.sleep(0.03)
        if chunked and cut is None:
            sock.sendall(b'0\r\n' + (b'\r\n' if finish_chunks else b''))
        # EOF on input while keeping the receive side open for the exact status.
        if cut is not None or not finish_chunks:
            sock.shutdown(socket.SHUT_WR)
        response = http.client.HTTPResponse(sock)
        response.begin()
        payload = response.read()
        assert response.status == 200 and payload[:2] == b'\x02\x00', payload
        return struct.unpack('>H', payload[2:4])[0]


def latest_job(port):
    with socket.create_connection(('127.0.0.1', port), timeout=10) as sock:
        sock.sendall(b'GET /api/state HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
        response = http.client.HTTPResponse(sock)
        response.begin()
        assert response.status == 200
        return json.loads(response.read())['printer']['jobs'][0]


def check_job(port, state, pages):
    job = latest_job(port)
    assert (job['state'], job['pages']) == (state, pages), job


def cancel_during_upload(port, urf):
    # The first page is already in the engine and the next page needs more
    # raster data. The dashboard and a second IPP request must remain usable.
    deadline = time.monotonic() + 5
    while True:
        job = latest_job(port)
        if job['state'] == 'processing' and job['pages'] == 1:
            break
        assert time.monotonic() < deadline, job
        time.sleep(0.02)
    status, _ = ipp(port, 2, urf, dpi=300)
    assert status == 0x0507, f'second job should be busy: {status:#06x}'
    assert latest_job(port)['id'] == job['id']
    status, _ = ipp(port, 0x0039)  # Cancel-My-Jobs
    assert status == 0, hex(status)


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
            white = pwg_page(300)
            dense = pwg_page(300, dense=True)
            pwg = ipp_payload(port, b'RaS2' + white + dense + white + dense,
                              'image/pwg-raster')
            assert len(pwg) > 2 * 1024 * 1024
            assert send_job(port, pwg, slow=True) == 0
            check_job(port, 'completed', 4)

            assert send_job(port, pwg, cut=512 * 1024) == 0x0411
            check_job(port, 'aborted', 1)
            # Same raster truncation with a complete HTTP body.
            assert send_job(port, pwg[:512 * 1024]) == 0x0411
            check_job(port, 'aborted', 1)

            pwg600 = ipp_payload(port, b'RaS2' + pwg_page(600, dense=True),
                                 'image/pwg-raster')
            assert send_job(port, pwg600, chunked=True) == 0
            check_job(port, 'completed', 1)
            assert send_job(port, pwg600, chunked=True, finish_chunks=False) == 0x0411
            check_job(port, 'aborted', 1)

            urf = white_urf(1, dpi=300, width=2480, height=3508)
            # A correct URF page with a body shorter than Content-Length.
            padded = ipp_payload(port, urf + bytes(1024 * 1024))
            assert send_job(port, padded, slow=True) == 0
            check_job(port, 'completed', 1)
            assert send_job(port, padded, cut=512 * 1024) == 0x0411
            check_job(port, 'aborted', 1)

            # A clean HTTP EOF before the page count declared by URF.
            fewer_pages = urf[:8] + struct.pack('>I', 2) + urf[12:]
            assert send_job(port, ipp_payload(port, fewer_pages)) == 0x0411
            check_job(port, 'aborted', 1)

            assert send_job(port, pwg, during_upload=lambda: cancel_during_upload(port, urf)) == 0
            check_job(port, 'canceled', 1)

            status, _ = ipp(port, 2, white_urf(2, dpi=300, width=2480, height=3508), dpi=300)
            assert status == 0, f'next job failed after disconnect: {status:#06x}'
            check_job(port, 'completed', 2)
            print('PASS: >2 MiB PWG at 300 dpi, chunked PWG at 600 dpi, '
                  'truncation, busy/cancel and next-job recovery')
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == '__main__':
    main()
