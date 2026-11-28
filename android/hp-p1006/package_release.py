# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json,zipfile,shutil,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--apk',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
apk=a.out/'Viet-Phuong-Print-Connect-Android-0.1.1.apk';shutil.copyfile(a.apk,apk)
def info(file):return {'file':file.name,'size':file.stat().st_size,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()}
record={'version':'0.1.1','tag':'print-connect-android-v0.1.1','source_commit':os.environ['GITHUB_SHA'],'apk':info(apk)}
(a.out/'android-release.json').write_text(json.dumps(record,indent=2)+'\n')
(a.out/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+r['file']+'\n' for r in [record['apk']]))
shutil.copyfile(Path(__file__).with_name('README.md'),a.out/'INSTALLATION.md')
print(json.dumps(record,indent=2))
