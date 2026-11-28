"""Stage an APK-only Android update; retain the pinned ESP firmware unchanged."""
import argparse,hashlib,json,shutil,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--release-dir',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
pinned=json.loads((root/'installer/android-app-release.json').read_text())
assert json.loads((a.release_dir/'android-release.json').read_text())==pinned
info=pinned['apk'];name=info['file'];assert Path(name).name==name
file=a.release_dir/name;assert file.stat().st_size==info['size'];assert hashlib.sha256(file.read_bytes()).hexdigest()==info['sha256']
with zipfile.ZipFile(file) as z:assert {'AndroidManifest.xml','classes.dex'}.issubset(z.namelist())
software=root/'installer/public/software';software.mkdir(parents=True,exist_ok=True);shutil.copyfile(file,software/name)
(software/'android-app-release.json').write_text(json.dumps(pinned,indent=2)+'\n')
print('APK-only Android update staged; no ESP firmware or Windows files changed.')
