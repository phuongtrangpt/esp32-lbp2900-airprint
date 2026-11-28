"""Stage verified Android APK and optional Print Connect ESP UI update only."""
import argparse,hashlib,json,shutil,subprocess,sys,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--release-dir',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
pinned=json.loads((root/'installer/android-release.json').read_text())
assert json.loads((a.release_dir/'android-release.json').read_text())==pinned
for info in [pinned['apk'],pinned['firmware']]:
 name=info['file'];assert Path(name).name==name
 file=a.release_dir/name;assert file.stat().st_size==info['size'];assert hashlib.sha256(file.read_bytes()).hexdigest()==info['sha256']
with zipfile.ZipFile(a.release_dir/pinned['apk']['file']) as apk:
 assert {'AndroidManifest.xml','classes.dex'}.issubset(apk.namelist())
source=root/'firmware/print-connect-android';source.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(a.release_dir/pinned['firmware']['file']) as z:
 for info in z.infolist():
  assert Path(info.filename).name==info.filename and info.file_size<32*1024*1024
  (source/info.filename).write_bytes(z.read(info))
m=json.loads((source/'manifest.json').read_text());assert m['profile']=='hp_pc' and m['version']==pinned['firmware']['version']
dest=root/'installer/public/firmware/hp_pc'
subprocess.run([sys.executable,str(root/'tools/prepare_web_installer.py'),'--source',str(source),'--dest',str(dest)],check=True)
web=json.loads((dest/'web-manifest.json').read_text());web.update(usbOnly=False,hpBootstrapEmbedded=True,name='HP P1006 Print Connect ESP 32')
(dest/'web-manifest.json').write_text(json.dumps(web,indent=2)+'\n')
ota=json.loads((dest/'ota-manifest.json').read_text())
ota['notes']='Print Connect cho HP P1006: link tải Windows và Android ngay trên trang in. Giữ nguyên giao thức USB, Wi-Fi và IP đã lưu.'
(dest/'ota-manifest.json').write_text(json.dumps(ota,indent=2,ensure_ascii=False)+'\n')
for name in ['firmware-merged.bin']:(dest/name).unlink(missing_ok=True)
(dest/'SHA256SUMS').write_text(''.join(hashlib.sha256(file.read_bytes()).hexdigest()+'  '+file.name+'\n' for file in sorted(dest.iterdir()) if file.name!='SHA256SUMS'))
software=root/'installer/public/software';software.mkdir(parents=True,exist_ok=True)
shutil.copyfile(a.release_dir/pinned['apk']['file'],software/pinned['apk']['file'])
(software/'android-release.json').write_text(json.dumps(pinned,indent=2)+'\n')
assert not list((root/'installer/public').rglob('*.zip'))
print('Android APK and Print Connect ESP download-card UI staged; Windows final and stable model firmware untouched.')
