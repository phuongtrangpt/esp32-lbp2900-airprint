from pathlib import Path
import shutil,tempfile,zipfile,subprocess,re
ROOT=Path(__file__).resolve().parents[1]
def main():
 source=ROOT/'installer/dist';exe=ROOT/'release/PrintServer-Offline.exe'
 if not exe.exists() or any(not (source/f'firmware/{p}/web-manifest.json').exists() for p in ('hp','canon','pixma','standard')):raise SystemExit('Build all profiles, installer and Windows launcher first')
 dest=source/'downloads';dest.mkdir(exist_ok=True)
 version=re.search(r'set\(PROJECT_VER "(\d+\.\d+\.\d+)',(ROOT/'CMakeLists.txt').read_text()).group(1)
 archive=dest/('ESP32-PrintServer-Offline-v'+version+'.zip')
 with tempfile.TemporaryDirectory() as td:
  folder=Path(td)/'ESP32-PrintServer-Offline';shutil.copytree(source,folder,ignore=shutil.ignore_patterns('downloads'))
  shutil.copyfile(exe,folder/exe.name);
  shutil.copyfile(ROOT/'release/LICENSE-Go.txt',folder/'LICENSE-Go.txt');
  (folder/'source-code.tar.gz').write_bytes(subprocess.check_output(['git','archive','--format=tar.gz','HEAD'],cwd=ROOT));shutil.copyfile(ROOT/'tools/offline/README.txt',folder/'README.txt')
  (folder/'START.cmd').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nPrintServer-Offline.exe\r\npause\r\n')
  (folder/'START_CANON.cmd').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nPrintServer-Offline.exe --skip-hp\r\npause\r\n')
  with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
   for p in folder.rglob('*'):
    if p.is_file():z.write(p,p.relative_to(Path(td)))
 print(archive)
if __name__=='__main__':main()
