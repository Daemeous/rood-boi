#!/usr/bin/env python3
"""Opt-in actual pack smoke: boot, match replacements and compare toggled renders.
ROOD_TEXTURE_PACK must name an extracted Beetle PNG pack. Uses private cards.
"""
import json, os, pathlib, sys, time
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'pc/tools'),str(ROOT/'pc/tests')]
from checkpoints import launch_at
from rood_rpc import OUT
from PIL import Image
import numpy as np
out=OUT/'hd-smoke';out.mkdir(parents=True,exist_ok=True)
assert os.environ.get('ROOD_TEXTURE_PACK'), 'set ROOD_TEXTURE_PACK to an extracted pack'
checkpoint=os.environ.get('ROOD_HD_CHECKPOINT','opening')
settings=out/'settings.cfg';settings.write_text('aspect 4:3\nhd_textures 1\n')
extra={'ROOD_SCALE':'2','ROOD_GI':'0','ROOD_SETTINGS':str(settings),'ROOD_WINDOW':'640x480'}
g=launch_at(checkpoint,card=os.environ.get('ROOD_HD_CARD'),fast=True,log=out/'game.log',extra_env=extra)
try:
    g.call('graphics',interpolate=False,aa='off',ui_upscale='nearest',ui_text='original')
    g.wait_presents(3)
    stats=g.call('hd_textures');print(json.dumps(stats),flush=True)
    assert stats['indexed']>0 and stats['matched_uploads']>0 and stats['resident']>0, 'no actual pack replacements matched'
    def capture(label,enabled):
        g.call('hd_textures',enabled=enabled);g.call('redraw');g.wait_presents(4)
        path=(out/f'{label}.ppm').resolve();path.unlink(missing_ok=True)
        g.call('present_dump',path=str(path))
        end=time.monotonic()+15
        while not path.exists() and time.monotonic()<end: time.sleep(.05)
        im=Image.open(path).convert('RGB');im.save(out/f'{label}.png')
        return np.asarray(im).astype(np.int16)
    hd=capture('hd',True);base=capture('original',False);hd2=capture('hd-again',True)
    delta=np.abs(hd-base);changed=float((delta.max(axis=2)>5).mean())
    restored=float(np.abs(hd2-hd).mean())
    print(f'changed pixels {changed:.3%}; toggle round-trip mean error {restored:.3f}',flush=True)
    assert changed>.005, 'matched PNGs did not change the rendered picture'
    assert restored<1, 'switching replacements back on did not restore the picture'
    print('PASS: real pack matched, rendered, and survived the on/off/on comparison',flush=True)
finally:g.close()
