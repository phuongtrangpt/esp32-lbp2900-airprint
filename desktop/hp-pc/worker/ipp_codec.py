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
    size = [8]
    def entry():
        t = f.read(1)
        if not t:
            raise ValueError('Missing IPP end attributes')
        tag = t[0]
        size[0] += 1
        if tag <= 0x0f:
            return tag, '', b''
        def take(n):
            size[0] += n
            if size[0] > 65536:
                raise ValueError('IPP attributes too large')
            b = f.read(n)
            if len(b) != n:
                raise ValueError('Truncated IPP attribute')
            return b
        n = struct.unpack('!H', take(2))[0]
        name = take(n).decode('ascii')
        n = struct.unpack('!H', take(2))[0]
        return tag, name, take(n)
    def collection(depth=0):
        if depth > 4:
            raise ValueError('IPP collection nested too deeply')
        members, member = {}, ''
        while True:
            tag, name, value = entry()
            if name:
                raise ValueError('Named collection entry')
            if tag == 0x37 and not value:
                return members
            if tag == 0x4a:
                member = value.decode('ascii')
                continue
            if tag < 0x10 or not member:
                raise ValueError('Malformed collection')
            if tag == 0x34:
                if value:
                    raise ValueError('Nonempty collection start')
                value = collection(depth + 1)
            members.setdefault(member, []).append((tag, value))
    attrs, last = {}, ''
    while size[0] < 65536:
        tag, name, value = entry()
        if tag == 3:
            return op, request, attrs
        if tag <= 0x0f:
            last = ''
            continue
        if name:
            last = name
        if not last:
            raise ValueError('IPP continuation without name')
        if tag == 0x34:
            if value:
                raise ValueError('Nonempty collection start')
            value = collection()
        attrs.setdefault(last, []).append((tag, value))
    raise ValueError('IPP attributes too large')


def raw(attrs, name, default=None):
    return attrs.get(name, [(0, default)])[0][1]


def text(attrs, name, default=''):
    v = raw(attrs, name, default.encode())
    if not isinstance(v, bytes):
        raise ValueError('Expected IPP text')
    return v.decode('utf-8', errors='replace')


def integer(attrs, name, default=0):
    v = raw(attrs, name)
    if v is None:
        return default
    if not isinstance(v, bytes) or len(v) != 4:
        raise ValueError('Expected IPP integer')
    return struct.unpack('!i', v)[0]


def reply(request, status=0, groups=b''):
    return struct.pack('!2sHI', b'\x02\x00', status, request) + b'\x01' + attr(0x47, 'attributes-charset', 'utf-8') + attr(0x48, 'attributes-natural-language', 'en') + groups + b'\x03'


def collection(name, members):
    data = attr(0x34, name, b'')
    for key, tag, value in members:
        data += attr(0x4a, '', key)
        data += collection('', value) if tag == 0x34 else attr(tag, '', value)
    return data + attr(0x37, '', b'')
