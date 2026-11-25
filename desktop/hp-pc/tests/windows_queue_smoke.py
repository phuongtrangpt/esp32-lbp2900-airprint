# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows spooler integration where the CI image supplies an IPP class driver.
No hardware: patch only the final USB transport; actual Windows raster and native
converter run unchanged. Capability absence is reported explicitly.
"""
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'worker'))
from worker import Service, Server


def ps(script, timeout=60):
    return subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],capture_output=True,text=True,timeout=timeout)


def main():
    check=ps("if (-not (Get-Service Spooler -ErrorAction SilentlyContinue)) { exit 42 }; if (-not (Get-Command Add-Printer).Parameters.ContainsKey('IppURL')) { exit 42 }; Get-PrinterDriver | Where-Object Name -eq 'Microsoft IPP Class Driver' | Select-Object -ExpandProperty Name")
    if check.returncode==42 or 'Microsoft IPP Class Driver' not in check.stdout:
        print('WINDOWS QUEUE: NOT RUN — this runner does not provide a registered Microsoft IPP Class Driver / IPP cmdlet. Native Windows converter and packaged IPP simulation are tested separately.')
        print(check.stderr)
        return
    service=Service('127.0.0.1',sys.argv[1]);server=Server(('127.0.0.1',8632),service)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    captured=[]
    def usb(host,path,cancel,progress,connection):
        data=Path(path).read_bytes();assert data.startswith(b'\x1b%-12345X@PJL JOB');captured.append(data)
    try:
        with patch('worker.send_job',usb):
            added=ps("$ErrorActionPreference='Stop'; Start-Service Spooler; Add-Printer -Name 'HP PC TEST CI' -IppURL 'http://127.0.0.1:8632/ipp/print'; $p=Get-CimInstance Win32_Printer -Filter \"Name='HP PC TEST CI'\"; $r=Invoke-CimMethod -InputObject $p -MethodName PrintTestPage; if ($r.ReturnValue -ne 0) { throw 'PrintTestPage failed' }")
            if added.returncode:raise AssertionError(added.stdout+added.stderr)
            deadline=time.monotonic()+60
            while time.monotonic()<deadline:
                if captured:break
                if any(j['state']==8 for j in service.jobs.values()):raise AssertionError('Windows raster conversion failed')
                time.sleep(.1)
            assert len(captured)==1,'No completed Windows test page reached PC encoder'
            print('WINDOWS QUEUE: Microsoft IPP Class Driver -> actual test-page raster -> native HP encoder passed; USB simulated')
    finally:
        ps("Remove-Printer -Name 'HP PC TEST CI' -ErrorAction SilentlyContinue")
        server.shutdown();server.server_close();thread.join();service.stop()


if __name__=='__main__':main()
