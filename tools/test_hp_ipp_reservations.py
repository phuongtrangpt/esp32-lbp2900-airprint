#!/usr/bin/env python3
"""Exercise HP P1006 Create-Job reservations against the native IPP server."""
import concurrent.futures
import http.client
import os
import re
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

from test_capt_ipp import attribute, white_urf

ROOT = Path(__file__).resolve().parents[1]
BUSY = 0x0507
DOCUMENT = white_urf(1)


def request(port, op, job_id=None, document=b''):
    body = struct.pack('>BBHI', 2, 0, op, 7) + b'\x01'
    body += attribute(0x47, 'attributes-charset', 'utf-8')
    body += attribute(0x48, 'attributes-natural-language', 'en')
    body += attribute(0x45, 'printer-uri', f'ipp://localhost:{port}/ipp/print')
    if job_id is not None:
        body += attribute(0x21, 'job-id', struct.pack('>I', job_id))
    if op == 6:
        body += attribute(0x22, 'last-document', b'\x01')
    if document:
        body += attribute(0x49, 'document-format', 'image/urf')
    body += b'\x03' + document
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=15)
    try:
        conn.request('POST', '/ipp/print', body, {'Content-Type': 'application/ipp'})
        response = conn.getresponse()
        data = response.read()
        assert response.status == 200, response.status
        return struct.unpack_from('>H', data, 2)[0], data
    finally:
        conn.close()


def integer(data, name):
    tag = b'\x23' if name == b'job-state' else b'\x21'
    field = tag + struct.pack('>H', len(name)) + name + b'\x00\x04'
    return struct.unpack_from('>I', data, data.index(field) + len(field))[0]


def main():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]

    def create():
        status, data = request(port, 5)
        assert status == 0, hex(status)
        return integer(data, b'job-id')

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / 'out').mkdir()
        log_path = work / 'server.log'
        with log_path.open('w') as log:
            server = subprocess.Popen(
                [str(ROOT / 'host/airprint_host'), str(port)], cwd=work,
                env=dict(os.environ, PRINTER_DRIVER='hp-p1006',
                         AIRPRINT_HOST_OPEN_DELAY_US='1500000',
                         ASAN_OPTIONS='detect_leaks=0'),
                stdout=log, stderr=log)
            try:
                for _ in range(100):
                    assert server.poll() is None, log_path.read_text()
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.2):
                            break
                    except OSError:
                        time.sleep(.05)
                else:
                    raise RuntimeError('IPP server did not start')

                orphan = create()
                second = create()
                with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
                    printing = executor.submit(request, port, 6, second, DOCUMENT)
                    for _ in range(50):
                        status, data = request(port, 9, second)
                        assert status == 0
                        if integer(data, b'job-state') == 5:
                            break
                        time.sleep(.02)
                    else:
                        raise RuntimeError('Second job never started processing')
                    third = create()
                    assert request(port, 6, third, DOCUMENT)[0] == BUSY
                    assert request(port, 2, document=DOCUMENT)[0] == BUSY
                    status, _ = printing.result()
                    assert status == 0, (hex(status), log_path.read_text())

                fourth = create()
                start = time.monotonic()
                for _ in range(1182):
                    assert request(port, 5)[0] == BUSY
                time.sleep(max(0, 5.1 - (time.monotonic() - start)))
                assert request(port, 5)[0] == BUSY

                assert request(port, 2, document=DOCUMENT)[0] == 0
                assert request(port, 6, third, DOCUMENT)[0] == 0
                assert request(port, 8, fourth)[0] == 0
                status, data = request(port, 9, orphan)
                assert status == 0 and integer(data, b'job-state') == 3
                assert create() > fourth

                report = log_path.read_text()
                entries = re.findall(r'IPP (?:op 0x0005 -> status 0x0507|Create-Job busy: suppressed \d+)', report)
                assert 2 <= len(entries) < 10, entries
                print('HP IPP: abandoned reservation, next job, concurrent admission, '
                      'retry, direct printing, cancellation and busy log throttling passed')
            finally:
                server.terminate()
                server.wait(timeout=5)


if __name__ == '__main__':
    main()
