# SPDX-License-Identifier: GPL-3.0-or-later
"""Checked PC -> ESP transport. Never replay a job after any output."""
import secrets
import socket
import struct
import zlib

HEADER = struct.Struct('!8sIQIQ')
REPLY = struct.Struct('!8sIIQ')
FRAME = struct.Struct('!III')
CHUNK = 4096
MAX_BYTES = 256 * 1024 * 1024


def exact(s, n):
    data = bytearray()
    while len(data) < n:
        b = s.recv(n - len(data))
        if not b:
            raise ConnectionError('ESP closed connection')
        data.extend(b)
    return data


def response(s):
    magic, status, seq, accepted = REPLY.unpack(exact(s, REPLY.size))
    if magic != b'VPHPA01\n':
        raise ConnectionError('Wrong ESP protocol: install HP PC TEST firmware')
    return status, seq, accepted


def hello(host, port=9101):
    with socket.create_connection((host, port), timeout=5) as s:
        s.sendall(HEADER.pack(b'VPHPO01\n', 1, 0, 0, 0))
        status, _, _ = response(s)
        if status:
            raise ConnectionError(f'ESP not ready (status {status})')
    return True


class DeviceUnavailable(ConnectionError):
    """An explicit refusal before the first document frame: no print data submitted."""
    pass


def send_job(host, path, canceled, progress, connection=lambda s: None, port=9101):
    total, crc = 0, 0
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(65536), b''):
            total += len(block)
            crc = zlib.crc32(block, crc)
    if not 128 <= total <= MAX_BYTES:
        raise ValueError('Converted job exceeds transport limits')
    job_id = secrets.randbits(64) or 1
    sent = 0
    s = socket.create_connection((host, port), timeout=30)
    connection(s)
    try:
        s.sendall(HEADER.pack(b'VPHPO01\n', 2, total, crc, job_id))
        initial = response(s)
        if initial[0] in (101,102) and initial[2] == 0:
            raise DeviceUnavailable(f'ESP not ready (status {initial[0]}); no document bytes submitted')
        if initial != (0, 0, 0):
            raise ConnectionError('ESP refused job before data transfer')
        seq = 0
        with open(path, 'rb') as f:
            for data in iter(lambda: f.read(CHUNK), b''):
                if canceled.is_set():
                    raise InterruptedError('Canceled; earlier pages may already be printed')
                s.sendall(FRAME.pack(seq, len(data), zlib.crc32(data)) + data)
                sent += len(data)
                if response(s) != (1, seq, sent):
                    raise ConnectionError('ESP did not confirm USB frame')
                progress(sent, total)
                seq += 1
        if canceled.is_set():
            raise InterruptedError('Canceled before finalization')
        s.sendall(FRAME.pack(seq, 0, 0))
        try:
            result = response(s)
        except (OSError, ConnectionError):
            # Query only: no data/job replay, including after an ESP reset.
            with socket.create_connection((host, port), timeout=5) as query:
                query.sendall(HEADER.pack(b'VPHPO01\n', 3, 0, 0, job_id))
                result = response(query)
            if result[0] != 2 or result[2] != total:
                raise ConnectionError('USB outcome unknown. Check paper before manually resubmitting')
            return
        if result != (2, seq, total):
            raise ConnectionError('ESP rejected final job checksum/end record')
    except Exception as e:
        if sent:
            raise ConnectionError(f'{e}. No automatic retry; some pages may have printed') from e
        raise
    finally:
        connection(None)
        s.close()
