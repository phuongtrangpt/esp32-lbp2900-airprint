# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert the owner's supplied logo to app/installer/tray icons; preserve RGBA."""
from pathlib import Path
from PIL import Image
import shutil

def main():
    project=Path(__file__).resolve().parent
    source=project.parents[1]/'branding/viet-phuong-logo.png'
    root=project/'src-tauri/icons';root.mkdir(parents=True,exist_ok=True)
    logo=Image.open(source).convert('RGBA')
    logo.resize((256,256),Image.Resampling.LANCZOS).save(root/'icon.png')
    logo.resize((256,256),Image.Resampling.LANCZOS).save(root/'icon.ico',format='ICO',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
    shutil.copyfile(source,project/'ui/logo.png')

if __name__=='__main__':main()
