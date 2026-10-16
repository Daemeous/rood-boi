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
    extension='.dds' if '--dds' in sys.argv else '.png'
    if extension=='.dds':
        hdr=[0]*31;hdr[0]=124;hdr[1]=0x1007;hdr[2]=8;hdr[3]=16;hdr[18]=32;hdr[19]=0x41
        hdr[21]=32;hdr[22:26]=[255,65280,16711680,4278190080];hdr[26]=0x1000
        png=b'DDS '+struct.pack('<31I',*hdr)+bytes([240,20,10,255])*16*8
    for n in [0,16,256]:
        b=zlib.crc32(struct.pack('<'+'H'*n,*range(1,n+1))) if n else 0
        (out/f'{a:x}-{b:x}{extension}').write_bytes(png)
    exe=out/'probe'
    subprocess.run(['g++','-std=c++17','-O1','-ffunction-sections','-fdata-sections',
        '-I'+str(ROOT/'pc/third_party/SDL3/include'),str(ROOT/'pc/tests/hd_textures_probe.cpp'),
        str(ROOT/'pc/build/sdl3_64/libSDL3.a'),'-Wl,--gc-sections','-lm','-lpthread','-ldl','-o',str(exe)],check=True)
    subprocess.run([str(exe)]+([str(ROOT/'pc/shaders/vulkan/hd_textures.glsl')] if '--gpu' in sys.argv else []),env={**os.environ,'ROOD_TEXTURE_PACK':tmp},check=True)

    automatic_env = dict(os.environ)
    automatic_env.pop('ROOD_TEXTURE_PACK', None)
    subprocess.run([str(exe), '--no-pack'], cwd=tmp, env=automatic_env, check=True)
    pack = out/'texture-replacements'
    pack.mkdir()
    for image in out.glob('*'+extension):
        image.rename(pack/image.name)
    subprocess.run([str(exe)], cwd=tmp, env=automatic_env, check=True)
    subprocess.run([str(exe), '--no-pack'], cwd=tmp,
                   env={**automatic_env, 'ROOD_TEXTURE_PACK':str(out/'absent')}, check=True)
