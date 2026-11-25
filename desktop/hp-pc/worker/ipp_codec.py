# SPDX-License-Identifier: GPL-3.0-or-later
import struct


def attr(tag, name, value):
    name = name.encode()
    if isinstance(value, str):
        value = value.encode()
    if isinstance(value, bool):
        value = bytes([int(value)])
    if isinstance(value, int):
        value = struct.pack('!i', value)
    return bytes([tag]) + struct.pack('!H', len(name)) + name + struct.pack('!H', len(value)) + value


def values(tag, name, items):
    return b''.join(attr(tag, name if i == 0 else '', v) for i, v in enumerate(items))


def decode(f):
    header = f.read(8)
    if len(header) != 8:
        raise ValueError('Short IPP header')
    version, op, request = struct.unpack('!2sHI', header)
    if version not in (b'\x01\x01', b'\x02\x00') or not request:
        raise ValueError('Unsupported IPP version/request id')
    attrs, last, size = {}, '', 8
    while size < 65536:
        raw = f.read(1)
        if not raw:
            raise ValueError('Missing IPP end attributes')
        tag = raw[0]
        size += 1
        if tag == 3:
            return op, request, attrs
        if tag <= 0x0f:
            last = ''
            continue
        raw = f.read(2)
        if len(raw) != 2:
            raise ValueError('Truncated IPP attribute')
        n = struct.unpack('!H', raw)[0]
        name = f.read(n)
        raw = f.read(2)
        if len(name) != n or len(raw) != 2:
            raise ValueError('Truncated IPP name')
        v = struct.unpack('!H', raw)[0]
        value = f.read(v)
        size += 4 + n + v
        if len(value) != v or size > 65536:
            raise ValueError('Invalid IPP value length')
        if name:
            last = name.decode('ascii')
        if not last:
            raise ValueError('IPP continuation without name')
        attrs.setdefault(last, []).append((tag, value))
    raise ValueError('IPP attributes too large')


def raw(attrs, name, default=None):
    return attrs.get(name, [(0, default)])[0][1]


def text(attrs, name, default=''):
    return raw(attrs, name, default.encode()).decode('utf-8', errors='replace')


def integer(attrs, name, default=0):
    v = raw(attrs, name)
    return struct.unpack('!i', v)[0] if v is not None and len(v) == 4 else default


def reply(request, status=0, groups=b''):
    return struct.pack('!2sHI', b'\x02\x00', status, request) + b'\x01' + attr(0x47, 'attributes-charset', 'utf-8') + attr(0x48, 'attributes-natural-language', 'en') + groups + b'\x03'
