#!/usr/bin/env python3
"""Repeat the real HTTP parser with ESP worker, close and heap admission rules.

Heap values are injected into native-only policy code. CPU, actual allocation,
DMA, ESP radio and lwIP are not emulated by this test.
"""
import http.client
import json
import os
import socket
import subprocess
import time
from test_capt_ipp_transport import ROOT, ipp_payload, pwg_page, send_job, latest_job, read_ipp_status


def set_heap(path, free=72495, largest=31744):
    tmp = path.with_suffix('.new')
    tmp.write_text(f'{free} {largest}\n')
    os.replace(tmp, path)


def request(port):
    return (f'GET /api/state HTTP/1.1\r\nHost: localhost:{port}\r\n'
            'Connection: keep-alive\r\n\r\n').encode()


def refused(port):
    try:
        with socket.create_connection(('127.0.0.1', port), timeout=5) as sock:
            sock.sendall(request(port))
            data = sock.recv(4096)
            assert not data, f'Expected rejection, got {data[:100]!r}'
    except (ConnectionResetError, BrokenPipeError):
        pass


def probe_close(port):
    with socket.create_connection(('127.0.0.1', port), timeout=5) as sock:
        sock.sendall(request(port))
        response = http.client.HTTPResponse(sock)
        response.begin()
        assert response.status == 200 and response.getheader('Connection') == 'close'
        response.read()
        assert sock.recv(1) == b''


def send_chunk(sock, data):
    line = f'{len(data):x}\r\n'.encode()
    sock.sendall(line[:1]); sock.sendall(line[1:])
    for p in range(0,len(data),8191): sock.sendall(data[p:p+8191])
    sock.sendall(b'\r\n')


def low_heap_print(port, heap, doc, chunk_size, budget):
    set_heap(heap)
    assert send_job(port, ipp_payload(port, op=5, document_format='image/pwg-raster')) == 0
    job_id = latest_job(port)['id']
    prefix = ipp_payload(port, op=6, job_id=job_id, document_format='image/pwg-raster')
    with socket.create_connection(('127.0.0.1', port),timeout=120) as sock:
        sock.sendall((f'POST /ipp/print HTTP/1.1\r\nHost: localhost:{port}\r\n'
                      'Content-Type: application/ipp\r\nTransfer-Encoding: chunked\r\n'
                      'Connection: keep-alive\r\n\r\n').encode())
        send_chunk(sock,prefix)
        send_chunk(sock,doc[:chunk_size])
        deadline=time.monotonic()+15
        while True:
            job=latest_job(port)
            if job['state']=='processing' and job['bytes']>=65536: break
            assert time.monotonic()<deadline,job
            time.sleep(.02)
        set_heap(heap,*budget)
        refused(port)
        # No reconnect/retry of the active print socket: stream over the same fd.
        for p in range(chunk_size,len(doc),chunk_size):
            send_chunk(sock,doc[p:p+chunk_size])
            refused(port)
        sock.sendall(b'0\r\n\r\n')
        assert read_ipp_status(sock)==0
    set_heap(heap)
    job=latest_job(port)
    assert (job['state'],job['pages'],job['bytes'])==('completed',4,len(doc)),job
    return dict(chunk_size=chunk_size,internal_free=budget[0],largest=budget[1],pages=4,
                received=job['bytes'],result='PASS')


def worker_limit(port):
    sockets=[]
    try:
        for _ in range(3):
            sockets.append(socket.create_connection(('127.0.0.1',port),timeout=5))
        time.sleep(.2)
        refused(port)
    finally:
        for s in sockets:s.close()
    time.sleep(.1)
    probe_close(port)


def main():
    rounds=int(os.environ.get('TEST_ESP_ROUNDS','5'))
    doc=b'RaS2'+b''.join(pwg_page(300,dense=True) for _ in range(4))
    out=ROOT/'host/out';out.mkdir(exist_ok=True)
    cases=[]
    for r in range(1,rounds+1):
        heap=out/f'esp-policy-heap-{r}.txt';set_heap(heap)
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        env=dict(os.environ,PRINTER_DRIVER='canon-lbp2900',ASAN_OPTIONS='detect_leaks=0',
                 UBSAN_OPTIONS='halt_on_error=1',AIRPRINT_HOST_SLOW_US='2000',
                 AIRPRINT_TEST_HEAP_FILE=str(heap))
        log_path=out/f'esp-policy-{r}.log'
        with log_path.open('w') as log:
            server=subprocess.Popen([str(ROOT/'host/airprint_esp_policy_host'),str(port)],
                                    cwd=ROOT/'host',env=env,stdout=log,stderr=log)
            try:
                for _ in range(100):
                    assert server.poll() is None
                    try:
                        with socket.create_connection(('127.0.0.1',port),timeout=.2):break
                    except OSError:time.sleep(.1)
                else:raise AssertionError('server not ready')
                probe_close(port)
                worker_limit(port)
                for size,budget in [(524288,(33323,28672)),(1048576,(35211,31744)),
                                    (524288,(50000,14335))]:
                    result=low_heap_print(port,heap,doc,size,budget)
                    result['round']=r;cases.append(result)
                # Recovery from a real missing document tail remains bounded.
                bad=ipp_payload(port,doc[:524288],document_format='image/pwg-raster')
                assert send_job(port,bad,chunked=True)==0x0411
                assert latest_job(port)['state']=='aborted'
                probe_close(port)
                print(f'round {r}/{rounds}: PASS; 3 workers, close after request, low-heap control rejection, print socket intact',flush=True)
            finally:
                server.terminate()
                try:server.wait(timeout=5)
                except subprocess.TimeoutExpired:server.kill();server.wait()
        text=log_path.read_text()
        assert 'HTTP connection refused (low internal RAM;' in text
        assert 'HTTP connection refused (worker limit;' in text
        assert 'ERROR: AddressSanitizer' not in text
    result=dict(rounds=rounds,completed_jobs=len(cases),completed_pages=len(cases)*4,
                document_bytes=len(doc),cases=cases,
                limitation='Native policy emulation: no actual ESP allocator, Xtensa CPU, DMA, radio or lwIP.')
    (out/'esp-policy-results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'PASS: {len(cases)} complete jobs / {len(cases)*4} pages despite rejected control sockets')

if __name__=='__main__':main()
