# SPDX-License-Identifier: GPL-3.0-or-later
"""Loopback IPP spooler. Full conversion precedes ESP job acquisition."""
import argparse
import collections
import ipaddress
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from ipp_codec import attr, values, decode, text, raw, integer, reply
from transport import MAX_BYTES, hello, send_job

MEDIA = ['iso_a4_210x297mm', 'na_letter_8.5x11in', 'iso_a5_148x210mm', 'iso_a6_105x148mm']
URI = 'http://127.0.0.1:8632/ipp/print'
_print_lock = threading.Lock()


def emit(phase, **data):
    with _print_lock:
        print(json.dumps(dict(phase=phase, **data), ensure_ascii=False), flush=True)


class IppError(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


class Service:
    def __init__(self, host, renderer):
        self.host, self.renderer = host, str(renderer)
        self.root = Path(tempfile.mkdtemp(prefix='hp-pc-test-'))
        self.jobs = collections.OrderedDict()
        self.lock = threading.RLock()
        self.pending = queue.Queue(maxsize=2)
        self.next_id = 0
        self.shutdown = threading.Event()
        self.active = None
        self.thread = threading.Thread(target=self.process, daemon=True)
        self.thread.start()

    def validate(self, a, document=False):
        if document and text(a, 'document-format', 'image/pwg-raster') not in ('image/pwg-raster', 'image/urf'):
            raise IppError(0x040a, 'Only PWG raster and URF are supported')
        if text(a, 'compression', 'none') != 'none':
            raise IppError(0x040f, 'Compression must be none')
        if integer(a, 'copies', 1) != 1 or text(a, 'sides', 'one-sided') != 'one-sided':
            raise IppError(0x040b, 'One copy, simplex only')
        if text(a, 'media', MEDIA[0]) not in MEDIA:
            raise IppError(0x040b, 'Unsupported paper')
        if raw(a, 'media-col') is not None:
            raise IppError(0x040b, 'Use the named media attribute, not media-col')
        resolution = raw(a, 'printer-resolution')
        if resolution is not None and resolution not in (struct.pack('!iiB', d, d, 3) for d in (300, 600)):
            raise IppError(0x040b, 'Use 300 or 600 dpi')
        if text(a, 'print-color-mode', 'monochrome') not in ('monochrome', 'auto'):
            raise IppError(0x040b, 'Monochrome only')

    def new_job(self, a):
        with self.lock:
            if sum(j['state'] < 7 for j in self.jobs.values()) >= 3:
                raise IppError(0x0507, 'Test queue full')
            while len(self.jobs) >= 32:
                terminal = next((i for i, j in self.jobs.items() if j['state'] >= 7), None)
                if terminal is None:
                    raise IppError(0x0507, 'Queue full')
                self.jobs.pop(terminal)
            self.next_id += 1
            j = dict(id=self.next_id, name=text(a, 'job-name', 'Windows document')[:128], state=4,
                     reason='job-data-insufficient', message='Waiting for document', cancel=threading.Event(),
                     connection=None, process=None, submitted=False, created=time.monotonic())
            self.jobs[j['id']] = j
            return j

    def find(self, a):
        i = integer(a, 'job-id')
        if not i:
            try:
                i = int(text(a, 'job-uri').rsplit('/', 1)[-1])
            except ValueError:
                pass
        with self.lock:
            if i not in self.jobs:
                raise IppError(0x0406, 'Job not found')
            return self.jobs[i]

    def cancel(self, j):
        with self.lock:
            if j['state'] >= 7:
                return
            j['cancel'].set()
            j['state'], j['reason'], j['message'] = 7, 'job-canceled-by-user', 'Canceled; check paper before resubmitting'
            if j['process']:
                try:
                    j['process'].terminate()
                except OSError:
                    pass
            if j['connection']:
                try:
                    j['connection'].shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        emit('canceled', job=j['id'], message=j['message'])

    def submit(self, j, source):
        with self.lock:
            if j['state'] >= 7 or j['submitted']:
                raise IppError(0x0405, 'Job already submitted/canceled; no automatic replay')
            path = self.root / f"{j['id']}.raster"
            with path.open('wb') as out:
                shutil.copyfileobj(source, out, 65536)
            j['submitted'] = True
            j['state'], j['reason'], j['message'] = 3, 'none', 'Spooling complete'
            try:
                self.pending.put_nowait(j)
            except queue.Full:
                path.unlink(missing_ok=True)
                j['state'] = 8
                raise IppError(0x0507, 'Queue full')
        emit('spooled', job=j['id'], message='Document received completely; rendering on PC')

    def process(self):
        while not self.shutdown.is_set():
            try:
                j = self.pending.get(timeout=0.25)
            except queue.Empty:
                # Expire Create-Job sessions that never sent a document.
                with self.lock:
                    for job in self.jobs.values():
                        if job['state'] == 4 and time.monotonic() - job['created'] > 120:
                            self.cancel(job)
                continue
            inp, out = self.root / f"{j['id']}.raster", self.root / f"{j['id']}.xqx"
            self.active = j
            try:
                if j['cancel'].is_set():
                    continue
                with self.lock:
                    j['state'], j['reason'], j['message'] = 5, 'job-transforming', 'Rendering on PC'
                    emit('rendering', job=j['id'])
                    j['process'] = subprocess.Popen([self.renderer, str(inp), str(out)], stdout=subprocess.PIPE,
                                                     stderr=subprocess.PIPE, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                stdout, stderr = j['process'].communicate(timeout=300)
                if j['cancel'].is_set():
                    raise InterruptedError('Canceled')
                if j['process'].returncode:
                    raise ValueError(stderr.decode(errors='replace')[-1000:] or 'Raster conversion failed')
                info = json.loads(stdout)
                emit('rendered', job=j['id'], pages=info['pages'], bytes=out.stat().st_size)
                j['message'], j['reason'] = 'Sending compressed data to ESP', 'job-printing'
                def connection(s):
                    with self.lock:
                        j['connection'] = s
                        if s is not None and j['cancel'].is_set():
                            s.close()
                last = [0.0]
                def progress(done, total):
                    now = time.monotonic()
                    if now - last[0] > 0.2 or done == total:
                        emit('sending', job=j['id'], done=done, total=total)
                        last[0] = now
                send_job(self.host, out, j['cancel'], progress, connection)
                with self.lock:
                    if not j['cancel'].is_set():
                        j['state'], j['reason'], j['message'] = 9, 'job-completed-successfully', 'USB delivered; paper output not confirmed'
                        emit('usb-delivered', job=j['id'], pages=info['pages'], message=j['message'])
            except Exception as e:
                with self.lock:
                    if j['process'] and j['process'].poll() is None:
                        j['process'].kill()
                        j['process'].communicate()
                    if not j['cancel'].is_set():
                        j['state'], j['reason'], j['message'] = 8, 'aborted-by-system', str(e)
                        emit('failed', job=j['id'], message=str(e))
            finally:
                j['connection'], j['process'] = None, None
                for path in (inp, out, Path(str(out) + '.tmp')):
                    path.unlink(missing_ok=True)
                self.active = None
                self.pending.task_done()

    def stop(self):
        self.shutdown.set()
        for j in list(self.jobs.values()):
            self.cancel(j)
        self.thread.join(timeout=35)
        if not self.thread.is_alive():
            shutil.rmtree(self.root, ignore_errors=True)

    def job_attrs(self, j):
        return b'\x02' + attr(0x21, 'job-id', j['id']) + attr(0x45, 'job-uri', URI + '/jobs/' + str(j['id'])) + attr(0x45, 'job-printer-uri', URI) + attr(0x42, 'job-name', j['name']) + attr(0x23, 'job-state', j['state']) + attr(0x44, 'job-state-reasons', j['reason']) + attr(0x41, 'job-state-message', j['message'])

    def printer_attrs(self):
        with self.lock:
            count = sum(j['state'] < 7 for j in self.jobs.values())
        d = b'\x04'
        for tag, name, value in [(0x45, 'printer-uri-supported', URI), (0x42, 'printer-name', 'HP P1006 PC TEST'),
              (0x41, 'printer-info', 'Windows rendering / ESP USB transport TEST'), (0x41, 'printer-make-and-model', 'HP P1006 PC TEST'),
              (0x45, 'printer-uuid', 'urn:uuid:13cf9f52-e673-43ac-9c2f-3293ca665c17'),
              (0x23, 'printer-state', 4 if count else 3), (0x44, 'printer-state-reasons', 'none'),
              (0x22, 'printer-is-accepting-jobs', count < 3), (0x21, 'queued-job-count', count),
              (0x22, 'color-supported', False), (0x44, 'sides-default', 'one-sided'), (0x44, 'sides-supported', 'one-sided'),
              (0x44, 'print-color-mode-default', 'monochrome'), (0x44, 'print-color-mode-supported', 'monochrome'),
              (0x44, 'media-default', MEDIA[0]), (0x49, 'document-format-default', 'image/pwg-raster'),
              (0x44, 'compression-supported', 'none'), (0x21, 'copies-default', 1),
              (0x33, 'copies-supported', struct.pack('!ii', 1, 1)), (0x44, 'uri-authentication-supported', 'none'),
              (0x44, 'uri-security-supported', 'none'), (0x22, 'multiple-document-jobs-supported', False),
              (0x21, 'multiple-operation-time-out', 120), (0x44, 'multiple-operation-time-out-action', 'abort-job'),
              (0x21, 'printer-up-time', int(time.monotonic())), (0x47, 'charset-configured', 'utf-8'),
              (0x47, 'charset-supported', 'utf-8'), (0x48, 'natural-language-configured', 'en'),
              (0x48, 'generated-natural-language-supported', 'en'),
              (0x32, 'printer-resolution-default', struct.pack('!iiB', 300, 300, 3)),
              (0x44, 'pwg-raster-document-sheet-back', 'normal'), (0x44, 'pwg-raster-document-type-supported', 'sgray_8')]:
            d += attr(tag, name, value)
        d += values(0x23, 'operations-supported', [2, 4, 5, 6, 8, 9, 10, 11])
        d += values(0x44, 'ipp-versions-supported', ['1.1', '2.0'])
        d += values(0x49, 'document-format-supported', ['image/pwg-raster', 'image/urf'])
        d += values(0x44, 'media-supported', MEDIA)
        d += values(0x32, 'printer-resolution-supported', [struct.pack('!iiB', d, d, 3) for d in (300, 600)])
        d += values(0x32, 'pwg-raster-document-resolution-supported', [struct.pack('!iiB', d, d, 3) for d in (300, 600)])
        d += values(0x44, 'urf-supported', ['V1.4', 'CP1', 'W8', 'RS300-600', 'DM1'])
        d += values(0x44, 'job-creation-attributes-supported', ['job-name', 'copies', 'media', 'sides', 'printer-resolution', 'print-color-mode'])
        return d

    def handle(self, body):
        op, request, a = decode(body)
        try:
            if op == 11:
                return reply(request, groups=self.printer_attrs())
            if op == 4:
                self.validate(a, True)
                return reply(request)
            if op in (2, 5):
                self.validate(a, op == 2)
                j = self.new_job(a)
                if op == 2:
                    try:
                        self.submit(j, body)
                    except Exception:
                        self.cancel(j)
                        raise
                return reply(request, groups=self.job_attrs(j))
            if op == 6:
                self.validate(a, True)
                if raw(a, 'last-document') != b'\x01':
                    raise IppError(0x040b, 'Exactly one final document required')
                j = self.find(a)
                self.submit(j, body)
                return reply(request, groups=self.job_attrs(j))
            if op in (8, 9):
                j = self.find(a)
                if op == 8:
                    self.cancel(j)
                return reply(request, groups=self.job_attrs(j))
            if op == 10:
                which = text(a, 'which-jobs', 'not-completed')
                with self.lock:
                    jobs = list(self.jobs.values())
                jobs = [j for j in jobs if which == 'all' or (j['state'] >= 7) == (which == 'completed')]
                return reply(request, groups=b''.join(self.job_attrs(j) for j in jobs))
            raise IppError(0x0501, 'Operation not supported')
        except IppError as e:
            emit('ipp-error', message=e.message)
            return reply(request, e.status)


class Server(ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = False
    def __init__(self, addr, service):
        self.service = service
        self.slots = threading.BoundedSemaphore(4)
        super().__init__(addr, Handler)
    def process_request(self, sock, addr):
        if not self.slots.acquire(blocking=False):
            sock.close()
            return
        try:
            super().process_request(sock, addr)
        except Exception:
            self.slots.release()
            raise
    def process_request_thread(self, sock, addr):
        try:
            super().process_request_thread(sock, addr)
        finally:
            self.slots.release()


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args):
        pass
    def setup(self):
        super().setup()
        self.connection.settimeout(30)
    def handle_expect_100(self):
        if self.path != '/ipp/print':
            self.send_error(404)
            return False
        return super().handle_expect_100()
    def do_POST(self):
        if self.path != '/ipp/print' or self.headers.get_content_type() != 'application/ipp':
            self.send_error(404)
            return
        try:
            # Bounded disk spool; never forward a prefix of an incomplete HTTP body.
            with tempfile.TemporaryFile(dir=self.server.service.root) as f:
                self.read_body(f)
                f.seek(0)
                result = self.server.service.handle(f)
            self.send_response(200)
            self.send_header('Content-Type', 'application/ipp')
            self.send_header('Content-Length', str(len(result)))
            self.end_headers()
            self.wfile.write(result)
        except (ValueError, UnicodeError, struct.error) as e:
            self.close_connection = True
            self.send_error(400, 'Invalid IPP/HTTP request')
            emit('upload-error', message=str(e))
        except (OSError, TimeoutError):
            self.close_connection = True
            emit('upload-error', message='Upload interrupted; nothing forwarded to ESP')
    def read_body(self, out):
        total = 0
        def copy(n):
            nonlocal total
            if n < 0 or total + n > MAX_BYTES:
                raise ValueError('Upload too large')
            remaining = n
            while remaining:
                b = self.rfile.read(min(65536, remaining))
                if not b:
                    raise ValueError('Truncated HTTP body')
                out.write(b)
                remaining -= len(b)
            total += n
        transfer = self.headers.get_all('Transfer-Encoding', [])
        lengths = self.headers.get_all('Content-Length', [])
        if transfer:
            if len(transfer) != 1 or transfer[0].lower() != 'chunked' or lengths:
                raise ValueError('Ambiguous HTTP framing')
            for _ in range(1000000):
                line = self.rfile.readline(256)
                if not line.endswith(b'\r\n'):
                    raise ValueError('Invalid HTTP chunk')
                token = line[:-2].split(b';', 1)[0]
                if not token or any(c not in b'0123456789abcdefABCDEF' for c in token):
                    raise ValueError('Invalid chunk size')
                n = int(token, 16)
                if not n:
                    used = 0
                    while True:
                        trailer = self.rfile.readline(4096)
                        used += len(trailer)
                        if not trailer.endswith(b'\r\n') or used > 16384:
                            raise ValueError('Invalid HTTP trailer')
                        if trailer == b'\r\n':
                            return
                copy(n)
                if self.rfile.read(2) != b'\r\n':
                    raise ValueError('Invalid chunk ending')
            raise ValueError('Too many HTTP chunks')
        if len(lengths) != 1 or not lengths[0].isdigit():
            raise ValueError('Content-Length required')
        copy(int(lengths[0]))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--esp', required=True)
    p.add_argument('--port', type=int, default=8632)
    p.add_argument('--renderer')
    args = p.parse_args()
    ipaddress.IPv4Address(args.esp)
    base = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
    renderer = args.renderer or base / ('hp-render.exe' if os.name == 'nt' else 'hp-render')
    if not Path(renderer).is_file():
        raise SystemExit('hp-render executable missing')
    service = Service(args.esp, renderer)
    server = Server(('127.0.0.1', args.port), service)
    def commands():
        for line in sys.stdin:
            command = line.strip()
            if command == 'cancel':
                if service.active:
                    service.cancel(service.active)
            elif command == 'stop':
                server.shutdown()
                return
        server.shutdown()
    threading.Thread(target=commands, daemon=True).start()
    emit('ready', message='Local IPP service running; keep this app open', uri=URI)
    try:
        server.serve_forever(poll_interval=0.2)
    finally:
        server.server_close()
        service.stop()


if __name__ == '__main__':
    main()
