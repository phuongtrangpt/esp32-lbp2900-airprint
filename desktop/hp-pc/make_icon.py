# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate reproducible app icons with the standard library (no build assets download)."""
from pathlib import Path
import struct
import zlib


def rgba(x, y):
    c=(22,30,44,255)
    if 17<=x<47 and 8<=y<27: c=(242,245,250,255)
    if 10<=x<54 and 24<=y<49: c=(255,208,90,255)
    if 18<=x<46 and 38<=y<57: c=(242,245,250,255)
    if 23<=x<41 and y in (44,49): c=(35,47,65,255)
    if 44<=x<48 and 29<=y<33: c=(35,47,65,255)
    return bytes(c)


def main():
    root=Path(__file__).resolve().parent/'src-tauri/icons';root.mkdir(parents=True,exist_ok=True)
    def chunk(kind,data):
        return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data))
    raw=b''.join(b'\0'+b''.join(rgba(x,y) for x in range(64)) for y in range(64))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',64,64,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    (root/'icon.png').write_bytes(png)
    pixels=b''.join(rgba(x,y)[2::-1]+b'\xff' for y in range(63,-1,-1) for x in range(64))
    mask=bytes(64*8)
    dib=struct.pack('<IiiHHIIiiII',40,64,128,1,32,0,len(pixels)+len(mask),0,0,0,0)+pixels+mask
    ico=struct.pack('<HHH',0,1,1)+struct.pack('<BBBBHHII',64,64,0,0,1,32,len(dib),22)+dib
    (root/'icon.ico').write_bytes(ico)


if __name__=='__main__':main()
