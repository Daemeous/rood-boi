#!/usr/bin/env python3
"""Independent zlib/system-xxHash fixtures for page, palette, DuckStation and DDS compatibility."""
import ctypes, ctypes.util, os, pathlib, struct, subprocess, tempfile, zlib, sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
lib=ctypes.CDLL(ctypes.util.find_library('xxhash'))
lib.XXH3_64bits.argtypes=[ctypes.c_void_p,ctypes.c_size_t];lib.XXH3_64bits.restype=ctypes.c_uint64
assert lib.XXH3_64bits(b'',0)==0x2d06800538d394c2
hash64=lambda b:lib.XXH3_64bits(b,len(b))
pack=lambda values:struct.pack('<'+'H'*len(values),*values)
def png(w,h):
    def chunk(k,d):return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([240,20,10,255])*w)*h))+chunk(b'IEND',b'')
def dds(data,fourcc=b'\0'*4,dxgi=None):
    hdr=[0]*31;hdr[0]=124;hdr[1]=0x1007;hdr[2]=hdr[3]=4;hdr[18]=32;hdr[19]=4 if fourcc!=b'\0'*4 else 0x41
    hdr[20]=int.from_bytes(fourcc,'little');hdr[21]=32
    hdr[22:26]=[255,65280,16711680,4278190080];hdr[26]=0x1000
    return b'DDS '+struct.pack('<31I',*hdr)+(struct.pack('<5I',dxgi,3,0,1,0) if dxgi else b'')+data
with tempfile.TemporaryDirectory(prefix='rood-formats-') as tmp:
    root=pathlib.Path(tmp);exe=root/'probe'
    subprocess.run(['g++','-std=c++17','-O1','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'pc/third_party/SDL3/include'),str(ROOT/'pc/tests/hd_formats_probe.cpp'),str(ROOT/'pc/build/sdl3_64/libSDL3.a'),'-Wl,--gc-sections','-lm','-lpthread','-ldl','-o',str(exe)],check=True)
    raw=pack([0x2121]*4);colors=pack(list(range(1,17)))
    page=bytearray(64*256*2)
    for y in range(2):page[y*128:y*128+4]=raw[:4]
    for mode in ['reduced','page','duckpage','duckupload','alias','dds']:
        d=root/mode;d.mkdir();images=d/'replacements';images.mkdir()
        if mode=='reduced':(images/f'{zlib.crc32(raw):x}-{zlib.crc32(colors[2:6]):x}.png').write_bytes(png(16,8))
        elif mode=='page':
            images=d/'texture-replacements-pages';images.mkdir()
            (images/f'{zlib.crc32(page):x}-{zlib.crc32(colors):x}.png').write_bytes(png(256,256))
        elif mode in ['duckpage','duckupload','alias']:
            kind='texupload' if mode=='duckupload' else 'texpage'
            h=hash64(raw) if kind=='texupload' else hash64(pack([0x2121]*2))
            size='2x2' if kind=='texupload' else '64x256'
            name=f'{kind}-STP4-{h:016X}-{hash64(colors):016X}-{size}-4-0-4x2-P0-15'
            if mode=='alias':
                (images/'renamed.png').write_bytes(png(8,4));(d/'config.yaml').write_text(f'Aliases:\n  {name}: |\n    renamed.png\n')
            else:(images/(name+'.png')).write_bytes(png(8,4))
        else:
            (d/'rgba.dds').write_bytes(dds(bytes([255,0,0,255])*16))
            block=struct.pack('<HHI',0xf800,0,0)
            (d/'bc1.dds').write_bytes(dds(block,b'DXT1'))
            (d/'bc2.dds').write_bytes(dds(b'\xff'*8+block,b'DXT3'))
            (d/'bc3.dds').write_bytes(dds(bytes([255,0])+b'\0'*6+block,b'DXT5'))
            fields=[(64,7),(127,7),(127,7),(0,7),(0,7),(0,7),(0,7),(127,7),(127,7),(1,1),(1,1),(0,3)]+[(0,4)]*15
            value=0;bit=0
            for v,n in fields:value|=v<<bit;bit+=n
            assert bit==128
            (d/'bc7.dds').write_bytes(dds(value.to_bytes(16,'little'),b'DX10',98))
            (d/'short.dds').write_bytes(dds(b'' ,b'DXT1'))
            huge=bytearray(dds(block,b'DXT1'));struct.pack_into('<I',huge,16,100000);(d/'huge.dds').write_bytes(huge)
            array=bytearray(dds(value.to_bytes(16,'little'),b'DX10',98));struct.pack_into('<I',array,140,2);(d/'array.dds').write_bytes(array)
            (d/'unknown.dds').write_bytes(dds(block,b'NOPE'))
        env={**os.environ,'ROOD_TEXTURE_PACK':str(d),'ROOD_TEXTURE_PACK_PAGES':str(d/'absent')}
        if "--gpu" in sys.argv:env["ROOD_HD_SHADER"]=str(ROOT/"pc/shaders/vulkan/hd_textures.glsl")
        subprocess.run([str(exe),mode],cwd=d,env=env,check=True)
