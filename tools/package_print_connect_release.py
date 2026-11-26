"""Package checked CI artifacts; no local credentials, no firmware rebuilding."""
import argparse,hashlib,json,shutil,zipfile,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--windows',type=Path,required=True);p.add_argument('--firmware',type=Path,required=True);p.add_argument('--out',type=Path,required=True);args=p.parse_args()
root=Path(__file__).resolve().parents[1]
version=json.loads((root/'desktop/hp-pc/src-tauri/tauri.conf.json').read_text())['version']
args.out.mkdir(parents=True,exist_ok=True)
exes=list(args.windows.rglob('*-setup.exe'));assert len(exes)==1,exes
installer=args.out/f'Viet-Phuong-Print-Connect-{version}-Setup.exe';shutil.copyfile(exes[0],installer)
manifest=json.loads((args.firmware/'manifest.json').read_text());assert manifest['profile']=='hp_pc';assert manifest['version']==version+'-hp-print-connect'
for line in (args.firmware/'SHA256SUMS').read_text().splitlines():
 digest,name=line.split(None,1);assert Path(name.strip()).name==name.strip();assert hashlib.sha256((args.firmware/name.strip()).read_bytes()).hexdigest()==digest
firmware=args.out/f'Print-Connect-ESP32-{version}.zip'
with zipfile.ZipFile(firmware,'w',zipfile.ZIP_DEFLATED) as z:
 for item in args.firmware.iterdir():
  if item.is_file():z.write(item,item.name)
assets={p.name:{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'size':p.stat().st_size} for p in [installer,firmware]}
record={'version':version,'source_commit':os.environ['GITHUB_SHA'],'tag':'print-connect-v'+version,'installer':installer.name,'firmware':firmware.name,'assets':assets}
(args.out/'release.json').write_text(json.dumps(record,indent=2)+'\n')
(args.out/'SHA256SUMS').write_text(''.join(info['sha256']+'  '+name+'\n' for name,info in assets.items()))
shutil.copyfile(root/'desktop/hp-pc/README.md',args.out/'INSTALLATION.md')
print(json.dumps(record,indent=2))
