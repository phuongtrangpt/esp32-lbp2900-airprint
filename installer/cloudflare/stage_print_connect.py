"""Stage a pinned, verified Print Connect release without touching existing models."""
import argparse,hashlib,json,shutil,subprocess,sys,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--release-dir',type=Path,required=True);args=p.parse_args()
root=Path(__file__).resolve().parents[2]
pinned=json.loads((root/'installer/print-connect-release.json').read_text())
actual=json.loads((args.release_dir/'release.json').read_text());assert actual==pinned,'Release metadata does not match pinned revision'
for name,info in pinned['assets'].items():
 assert Path(name).name==name
 path=args.release_dir/name;assert path.stat().st_size==info['size'];assert hashlib.sha256(path.read_bytes()).hexdigest()==info['sha256'],name
source=root/'firmware/print-connect-release';source.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(args.release_dir/pinned['firmware']) as z:
 for name in z.namelist():
  assert Path(name).name==name,name
  (source/name).write_bytes(z.read(name))
manifest=json.loads((source/'manifest.json').read_text());assert manifest['profile']=='hp_pc';assert manifest['version']==pinned['version']+'-hp-print-connect'
dest=root/'installer/public/firmware/hp_pc'
subprocess.run([sys.executable,str(root/'tools/prepare_web_installer.py'),'--source',str(source),'--dest',str(dest)],check=True)
web=json.loads((dest/'web-manifest.json').read_text());web.update(usbOnly=True,hpBootstrapEmbedded=True,name='HP P1006 Print Connect ESP 32')
(dest/'web-manifest.json').write_text(json.dumps(web,indent=2)+'\n')
for name in ['ota-manifest.json','firmware-merged.bin']:(dest/name).unlink(missing_ok=True)
(dest/'SHA256SUMS').write_text(''.join(hashlib.sha256(item.read_bytes()).hexdigest()+'  '+item.name+'\n' for item in sorted(dest.iterdir()) if item.name!='SHA256SUMS'))
software=root/'installer/public/software';software.mkdir(parents=True,exist_ok=True)
shutil.copyfile(args.release_dir/pinned['installer'],software/pinned['installer'])
(software/'release.json').write_text(json.dumps(pinned,indent=2)+'\n')
assert not list((root/'installer/public').rglob('*.zip'))
print('Pinned Print Connect Windows installer and online-only ESP firmware staged; existing model files unchanged.')
