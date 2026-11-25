# SPDX-License-Identifier: GPL-3.0-or-later
"""Install the actual NSIS bundle, exercise LocalService, close the GUI,
kill worker/host to test recovery, then uninstall. No real printer involved."""
import ctypes
from ctypes import wintypes
import http.client
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_offload import ipp,raster
from transport import exact,HEADER,FRAME,REPLY
SERVICE='VietPhuongHPP1006PC'
DATA=Path(os.environ['PROGRAMDATA'])/'VietPhuong/HP-P1006-PC'


def wait(fn,timeout=45):
    until=time.monotonic()+timeout
    last=None
    while time.monotonic()<until:
        try:
            value=fn()
            if value:return value
        except Exception as e:last=e
        time.sleep(.25)
    raise AssertionError(f'Timed out: {last}')


def status():
    return json.loads((DATA/'status.json').read_text())


def request(body):
    conn=http.client.HTTPConnection('127.0.0.1',8632,timeout=5)
    try:
        conn.request('POST','/ipp/print',body,headers={'Content-Type':'application/ipp'})
        r=conn.getresponse();data=r.read();assert r.status==200
        assert data[2:4]==b'\0\0',data[:8]
        return data
    finally:conn.close()


def scinfo():
    script=f"Get-CimInstance Win32_Service -Filter \"Name='{SERVICE}'\" | Select-Object State,StartMode,StartName,ProcessId | ConvertTo-Json -Compress"
    r=subprocess.run(['powershell.exe','-NoProfile','-Command',script],capture_output=True,text=True,check=True,timeout=15)
    return json.loads(r.stdout) if r.stdout.strip() else None


def windows(pid,visible=True):
    u=ctypes.windll.user32;result=[]
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    @callback
    def enum(hwnd,_):
        owner=wintypes.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if owner.value==pid and (not visible or u.IsWindowVisible(hwnd)):
            title=ctypes.create_unicode_buffer(512);u.GetWindowTextW(hwnd,title,512)
            if 'HP P1006' in title.value:result.append(hwnd)
        return True
    u.EnumWindows(enum,0);return result


def main():
    installer=Path(sys.argv[1]).resolve();gui=None
    if scinfo():raise AssertionError('Unexpected pre-existing service on fresh CI runner')
    with tempfile.TemporaryDirectory(prefix='hp-pc-service-',dir=os.environ.get('RUNNER_TEMP')) as dest:
        folder=Path(dest)/'app'
        subprocess.run([str(installer),'/S',f'/D={folder}'],check=True,timeout=180)
        helper=folder/'hp-pc-service.exe';app=folder/'hp-pc-test.exe'
        assert helper.is_file() and app.is_file(),list(folder.iterdir())
        try:
            info=wait(scinfo);assert info['StartMode']=='Auto',info
            assert info['StartName'].lower()==r'nt authority\localservice',info
            subprocess.run([str(helper),'--set-ip','127.0.0.1'],check=True,timeout=30)
            wait(lambda:status().get('ready'));wait(lambda:request(ipp(11)))
            gui=subprocess.Popen([str(app)])
            hwnd=wait(lambda:windows(gui.pid))[0]
            # X hides only the manager. The real Windows Service keeps serving.
            ctypes.windll.user32.PostMessageW(hwnd,0x0010,0,0)
            wait(lambda:not windows(gui.pid));assert gui.poll() is None
            request(ipp(11))
            second=subprocess.Popen([str(app)]);second.wait(timeout=15)
            assert second.returncode==0
            wait(lambda:windows(gui.pid))
            subprocess.run([str(app),'--quit'],check=True,timeout=15);gui.wait(timeout=15)
            request(ipp(11))
            print('SERVICE GUI: close-to-tray, single instance, and exit-manager without stopping IPP passed',flush=True)
            # A real LocalService-owned worker renders and sends a complete job.
            captured=[];errors=[]
            listener=socket.socket();listener.bind(('127.0.0.1',9101));listener.listen(1);listener.settimeout(30)
            def peer():
                try:
                    with listener.accept()[0] as s:
                        s.settimeout(20)
                        magic,op,total,crc,job=HEADER.unpack(exact(s,32));assert magic==b'VPHPO01\n' and op==2
                        s.sendall(REPLY.pack(b'VPHPA01\n',0,0,0));data=bytearray();seq=0
                        while True:
                            i,n,c=FRAME.unpack(exact(s,12));assert i==seq
                            if not n:
                                assert len(data)==total and zlib.crc32(data)==crc
                                s.sendall(REPLY.pack(b'VPHPA01\n',2,seq,len(data)));break
                            block=exact(s,n);assert zlib.crc32(block)==c;data.extend(block)
                            s.sendall(REPLY.pack(b'VPHPA01\n',1,seq,len(data)));seq+=1
                        captured.append(bytes(data))
                except Exception as e:errors.append(repr(e))
                finally:listener.close()
            t=threading.Thread(target=peer,daemon=True);t.start()
            request(ipp(doc=raster(2)))
            wait(lambda:bool(captured));t.join(timeout=5);assert not errors,errors
            wait(lambda:any(e.get('phase')=='usb-delivered' for e in status()['events']))
            assert captured[0].endswith(b'\x1b%-12345X@PJL EOJ\n\x1b%-12345X')
            print('SERVICE PRINT: no GUI, LocalService -> native HP converter -> simulated ESP passed',flush=True)
            old=status()['worker_pid'];subprocess.run(['taskkill','/F','/T','/PID',str(old)],check=True,capture_output=True)
            wait(lambda:status().get('ready') and status().get('worker_pid') not in (None,old))
            request(ipp(11));print('SERVICE RECOVERY: worker killed and automatically recovered',flush=True)
            old=scinfo()['ProcessId'];subprocess.run(['taskkill','/F','/PID',str(old)],check=True,capture_output=True)
            wait(lambda:(scinfo() or {}).get('ProcessId') not in (None,0,old),timeout=45)
            wait(lambda:status().get('ready'));request(ipp(11))
            print('SERVICE RECOVERY: host killed and Windows SCM automatically restarted it',flush=True)
        finally:
            if gui and gui.poll() is None:
                subprocess.run([str(app),'--quit'],timeout=15)
                try:gui.wait(timeout=10)
                except subprocess.TimeoutExpired:gui.kill();gui.wait()
            uninstall=next(folder.glob('*uninstall*.exe'),None)
            if uninstall:
                subprocess.run([str(uninstall),'/S'],check=True,timeout=90)
                wait(lambda:scinfo() is None)
                print('SERVICE UNINSTALL: service removed successfully',flush=True)
            else:
                subprocess.run([str(helper),'--remove'],timeout=45)
                raise AssertionError('NSIS uninstaller missing')
    print('WINDOWS SERVICE INSTALL/LIFECYCLE/PRINT/RECOVERY TESTS PASSED',flush=True)


if __name__=='__main__':main()
