#!/usr/bin/env python3
"""Repeat Create-Job/Send-Document with large HTTP chunks and live polling.

Native protocol tests do not emulate ESP internal RAM or lwIP scheduling.
"""
import hashlib
import json
import os
import socket
import struct
import subprocess
import time
from test_capt_ipp_transport import ROOT, ipp_payload, pwg_page, send_job, latest_job, read_ipp_status


def probe(port):
    return send_job(port, ipp_payload(port, op=0x0b))


def chunked_document(port, job_id, document, chunk_size, pause, polls):
    prefix = ipp_payload(port, op=6, job_id=job_id, document_format='image/pwg-raster')
    with socket.create_connection(('127.0.0.1', port), timeout=120) as sock:
        sock.sendall((f'POST /ipp/print HTTP/1.1\r\nHost: localhost:{port}\r\n'
                      'Content-Type: application/ipp\r\nTransfer-Encoding: chunked\r\n'
                      'Expect: 100-continue\r\nConnection: close\r\n\r\n').encode())
        # Read exactly the interim response without swallowing subsequent bytes.
        response = bytearray()
        while not response.endswith(b'\r\n\r\n'):
            b = sock.recv(1)
            assert b, 'EOF before 100 Continue'
            response += b
        assert response.startswith(b'HTTP/1.1 100 '), response
        blocks = [prefix] + [document[p:p + chunk_size] for p in range(0, len(document), chunk_size)]
        for block in blocks:
            # Deliberately split the size line across TCP sends.
            line = f'{len(block):x};test=windows-chunk\r\n'.encode()
            sock.sendall(line[:1])
            sock.sendall(line[1:])
            for off in range(0, len(block), 8191):
                sock.sendall(block[off:off + 8191])
            sock.sendall(b'\r\n')
            if pause and len(block) > 1000:
                time.sleep(pause)
            assert probe(port) == 0
            polls.append(1)
        sock.sendall(b'0\r\nTest-Trailer: complete\r\n\r\n')
        assert read_ipp_status(sock) == 0


def main():
    rounds = int(os.environ.get('TEST_WINDOWS_ROUNDS', '5'))
    doc = b'RaS2' + b''.join(pwg_page(300, dense=True) for _ in range(4))
    results = []
    (ROOT / 'host/out').mkdir(exist_ok=True)
    for round_index in range(rounds):
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]
        env = dict(os.environ, PRINTER_DRIVER='canon-lbp2900',
                   ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1',
                   AIRPRINT_HOST_SLOW_US='500')
        log_path = ROOT / f'host/out/windows-chunks-{round_index+1}.log'
        with log_path.open('w') as log:
            server = subprocess.Popen([str(ROOT / 'host/airprint_host'), str(port)],
                                      cwd=ROOT / 'host', env=env, stdout=log, stderr=log)
            try:
                for _ in range(100):
                    assert server.poll() is None
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.1): break
                    except OSError: time.sleep(.1)
                else: raise AssertionError('server not ready')
                for size in (512*1024, 1024*1024):
                    for pause in (0, 0.08):
                        assert send_job(port, ipp_payload(port, op=5, document_format='image/pwg-raster')) == 0
                        job_id = latest_job(port)['id']
                        polls = []
                        chunked_document(port, job_id, doc, size, pause, polls)
                        job = latest_job(port)
                        assert (job['state'],job['pages'],job['bytes']) == ('completed',4,len(doc)), job
                        results.append(dict(round=round_index+1,chunk_size=size,pause=pause,
                                            pages=4,bytes=job['bytes'],polls=len(polls),status='PASS'))
                # A genuinely truncated HTTP body must fail without poisoning the next job.
                bad = ipp_payload(port, doc[:512*1024], document_format='image/pwg-raster')
                assert send_job(port, bad, chunked=True) == 0x0411
                assert latest_job(port)['state'] == 'aborted'
                assert send_job(port, ipp_payload(port, doc, document_format='image/pwg-raster')) == 0
                assert latest_job(port)['pages'] == 4 and latest_job(port)['state'] == 'completed'
                print(f'round {round_index+1}/{rounds}: PASS; 512 KiB/1 MiB chunks, polling, delays, truncation recovery',flush=True)
            finally:
                server.terminate()
                try: server.wait(timeout=5)
                except subprocess.TimeoutExpired: server.kill(); server.wait()
        assert 'ERROR: AddressSanitizer' not in log_path.read_text()
    summary=dict(rounds=rounds,complete_chunked_jobs=len(results),complete_pages=len(results)*4,
                 document_bytes=len(doc),document_sha256=hashlib.sha256(doc).hexdigest(),results=results,
                 limitations='Native host: ESP internal RAM pressure and lwIP scheduling not emulated.')
    out=ROOT/'host/out/windows-chunks-results.json'
    out.write_text(json.dumps(summary,indent=2)+'\n')
    print(f'PASS: {len(results)} complete chunked jobs, {len(results)*4} pages; {rounds} truncation/recovery pairs')

if __name__=='__main__': main()
