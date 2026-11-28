"""Replace only Canon LBP2900 assets using the verified LED-only release."""
import argparse,hashlib,json,subprocess,sys,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--release-dir',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
pinned=json.loads((root/'installer/canon-led-release.json').read_text())
assert json.loads((a.release_dir/'canon-release.json').read_text())==pinned
name=pinned['file'];assert Path(name).name==name
archive=a.release_dir/name;assert archive.stat().st_size==pinned['size'];assert hashlib.sha256(archive.read_bytes()).hexdigest()==pinned['sha256']
source=root/'firmware/canon-led-release';source.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(archive) as z:
 for item in z.infolist():
  assert Path(item.filename).name==item.filename and item.file_size<32*1024*1024
  (source/item.filename).write_bytes(z.read(item))
m=json.loads((source/'manifest.json').read_text());assert m['profile']=='canon' and m['version']==pinned['version']
dest=root/'installer/public/firmware/canon'
subprocess.run([sys.executable,str(root/'tools/prepare_web_installer.py'),'--source',str(source),'--dest',str(dest)],check=True)
ota=json.loads((dest/'ota-manifest.json').read_text());ota['notes']='LBP2900: tắt RGB khi rảnh; nháy khi in, giữ cảnh báo lỗi và 5 nháy khi cần khởi động lại máy in. Chỉ thay điều khiển LED; mã CAPT/USB/xử lý in giữ nguyên.'
(dest/'ota-manifest.json').write_text(json.dumps(ota,indent=2,ensure_ascii=False)+'\n')
(dest/'firmware-merged.bin').unlink(missing_ok=True)
(dest/'SHA256SUMS').write_text(''.join(hashlib.sha256(file.read_bytes()).hexdigest()+'  '+file.name+'\n' for file in sorted(dest.iterdir()) if file.name!='SHA256SUMS'))
assert not list((root/'installer/public').rglob('*.zip'))
print('Canon LED-only update staged for USB and OTA; all other models untouched.')
