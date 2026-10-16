#!/usr/bin/env python3
"""Compile and test the Beetle matcher against independently generated zlib IDs and PNGs."""
import os, pathlib, struct, subprocess, tempfile, zlib, sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
def chunk(kind,data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
with tempfile.TemporaryDirectory(prefix='rood-hd-') as tmp:
    out=pathlib.Path(tmp)
    raw=struct.pack('<4H',0x3210,0x7654,0xba98,0xfedc)
    a=zlib.crc32(raw)
    ihdr=struct.pack('>2I5B',16,8,8,6,0,0,0)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',ihdr)+chunk(b'IDAT',zlib.compress((b'\0'+bytes([240,20,10,255])*16)*8))+chunk(b'IEND',b'')
    for n in [0,16,256]:
        b=zlib.crc32(struct.pack('<'+'H'*n,*range(1,n+1))) if n else 0
        (out/f'{a:x}-{b:x}.png').write_bytes(png)
    exe=out/'probe'
    subprocess.run(['g++','-std=c++17','-O1','-ffunction-sections','-fdata-sections',
        '-I'+str(ROOT/'pc/third_party/SDL3/include'),str(ROOT/'pc/tests/hd_textures_probe.cpp'),
        str(ROOT/'pc/build/sdl3_64/libSDL3.a'),'-Wl,--gc-sections','-lm','-lpthread','-ldl','-o',str(exe)],check=True)
    subprocess.run([str(exe)]+([str(ROOT/'pc/shaders/vulkan/hd_textures.glsl')] if '--gpu' in sys.argv else []),env={**os.environ,'ROOD_TEXTURE_PACK':tmp},check=True)
