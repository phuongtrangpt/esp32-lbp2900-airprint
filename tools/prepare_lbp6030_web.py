#!/usr/bin/env python3
"""Stage the pinned LBP6030 prerelease for USB installation."""
import argparse
import hashlib
import io
import json
from pathlib import Path
from urllib.request import urlopen
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parent.parent
URL = (
    "https://github.com/phuongtrangpt/esp32-lbp2900-airprint/releases/download/"
    "lbp6030-test-98f8f230/Canon-LBP6030-ESP32-EXPERIMENTAL.zip"
)
SHA256 = "47beefa04c1ec77fcfbb50d9ff0a928159764ddc12a0ea81cd2dafafaa0f3dd3"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    if args.archive:
        raw = args.archive.read_bytes()
    else:
        with urlopen(URL, timeout=60) as response:
            raw = response.read(4 * 1024 * 1024)
    if hashlib.sha256(raw).hexdigest() != SHA256:
        raise SystemExit("LBP6030 prerelease ZIP checksum mismatch")

    with ZipFile(io.BytesIO(raw)) as archive:
        info = json.loads(archive.read("manifest.json"))
        merged = archive.read("firmware-merged.bin")
        app = archive.read("firmware.bin")
        ota_selection = archive.read("ota_data_initial.bin")
        if (info["profile"] != "lbp6030" or info["chip"] != "esp32s3"
                or info["flash_size"] != "16MB" or info["physical_printer_tested"] is not False
                or merged[0xf000:0x11000] != ota_selection or merged[0x20000:] != app
                or app[80:112].split(b"\0")[0] != b"vietphuong_lbp6030"):
            raise SystemExit("Unexpected LBP6030 profile or flash layout")

        dest = ROOT / "installer/public/firmware/lbp6030"
        dest.mkdir(parents=True, exist_ok=True)
        parts = []
        for name, offset, data in (
            ("system.bin", 0, merged[:0x9000]),
            ("ota_data_initial.bin", 0xf000, ota_selection),
            ("firmware.bin", 0x20000, app),
        ):
            (dest / name).write_bytes(data)
            parts.append({"path": name, "offset": offset, "sha256": hashlib.sha256(data).hexdigest()})
        manifest = {
            "name": "Canon LBP6030 TEST ESP 32",
            "version": info["version"],
            "usbOnly": True,
            "new_install_prompt_erase": True,
            "new_install_improv_wait_time": 20,
            "builds": [{"chipFamily": "ESP32-S3", "serialType": "uart", "parts": parts}],
        }
        (dest / "web-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        for name in ("LICENSE", "LICENSE.upstream-MIT", "LICENSE.lbp6030-MIT", "LBP6030.md"):
            (dest / name).write_bytes(archive.read(name))
        print("LBP6030 prerelease verified and staged for web USB installation")


if __name__ == "__main__":
    main()
