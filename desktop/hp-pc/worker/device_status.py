# SPDX-License-Identifier: GPL-3.0-or-later
"""Read physical USB status from the existing ESP API, not just PC service health."""
import http.client
import json
import socket
from transport import HEADER, response


def offline(message):
    return dict(online=False, can_print=False, state='offline', message=message,
                reasons=['connecting-to-device-error'])


def ready_status(host,pc_port,timeout):
    with socket.create_connection((host,pc_port),timeout=timeout) as s:
        s.sendall(HEADER.pack(b'VPHPO01\n',1,0,0,0))
        status,_,_=response(s)
    if status == 101:
        return dict(online=True,can_print=False,state='busy',message='Printer is busy',reasons=['none'])
    if status != 0:
        return offline(f'Offline: ESP refused printer readiness (status {status})')
    return dict(online=True,can_print=True,state='ready',message='HP P1006 ready',reasons=['none'])


def probe_device(host, http_port=80, pc_port=9101, timeout=1.5):
    conn = http.client.HTTPConnection(host, http_port, timeout=timeout)
    try:
        conn.request('GET', '/api/state', headers={'Connection': 'close'})
        r = conn.getresponse()
        body = r.read(65537)
        if r.status != 200 or len(body) > 65536:
            raise ConnectionError('ESP status endpoint unavailable')
        p = json.loads(body)['printer']
        if p.get('connected') is not True:
            return offline('Offline: ESP cannot detect the USB printer; check printer power and USB cable')
        if 'p1006' not in str(p.get('model', '')).lower():
            return offline('Offline: ESP is not configured for HP P1006')
        reasons = [x for x in p.get('reasons', []) if isinstance(x,str) and x != 'none']
        if any(x.endswith('-error') for x in reasons):
            return dict(online=True, can_print=False, state='error', message=str(p.get('message') or 'Printer needs attention'), reasons=reasons)
        # Do not queue HELLO behind another PC's complete USB transfer.
        if p.get('state') == 'processing':
            return dict(online=True,can_print=False,state='busy',message='Printer is busy',reasons=['none'])
        return ready_status(host,pc_port,timeout)
    except (OSError,http.client.HTTPException,ValueError,KeyError):
        # During a direct IPP job the ESP can refuse extra HTTP workers to
        # reserve internal RAM. A short readiness query still reports busy.
        return ready_status(host,pc_port,timeout)
    finally:
        conn.close()
