# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json,zipfile,shutil,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--apk',type=Path,required=True);p.add_argument('--firmware',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
apk=a.out/'Viet-Phuong-Print-Connect-Android-0.1.0.apk';shutil.copyfile(a.apk,apk)
m=json.loads((a.firmware/'manifest.json').read_text());assert m['profile']=='hp_pc' and m['version']=='1.0.2-hp-print-connect'
for line in (a.firmware/'SHA256SUMS').read_text().splitlines():
 sha,name=line.split(None,1);name=name.strip();assert Path(name).name==name;assert hashlib.sha256((a.firmware/name).read_bytes()).hexdigest()==sha
fw=a.out/'Print-Connect-ESP32-1.0.2.zip'
with zipfile.ZipFile(fw,'w',zipfile.ZIP_DEFLATED) as z:
 for file in a.firmware.iterdir():
  if file.is_file():z.write(file,file.name)
def info(file):return {'file':file.name,'size':file.stat().st_size,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()}
record={'version':'0.1.0','tag':'print-connect-android-v0.1.0','source_commit':os.environ['GITHUB_SHA'],'apk':info(apk),'firmware':dict(info(fw),version=m['version'])}
(a.out/'android-release.json').write_text(json.dumps(record,indent=2)+'\n')
(a.out/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+r['file']+'\n' for r in [record['apk'],record['firmware']]))
shutil.copyfile(Path(__file__).with_name('README.md'),a.out/'INSTALLATION.md')
print(json.dumps(record,indent=2))
