#!/usr/bin/env python3
"""Native difficulty integration checks, using a disposable copy of a card."""
import argparse
import os
import pathlib
import struct
from callo_smoke import Run, ROOT, PROGRESS_FLAG
import rood_rpc

def actors(game):
    result=[]
    for slot,pointer in enumerate(struct.unpack('<16Q',game.read_symbol('vs_battle_actors',128))):
        if not 0x80010000<=pointer<0x80200000: continue
        header=game.read(pointer,0x40)
        stats=struct.unpack_from('<I',header,0x3c)[0]
        if not 0x80010000<=stats<0x80200000: continue
        data=game.read(stats,0x8c0)
        result.append(dict(slot=slot,model=header[0x18],spawn=header[0x27],address=stats,
            hp=struct.unpack_from('<H',data,0x18)[0],main=struct.unpack_from('<12H',data,0x18),
            limbs=[struct.unpack_from('<hh',data,0x398+i*0xdc) for i in range(6)]))
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',required=True)
    p.add_argument('--card',required=True,help='read-only source card; test uses a copy')
    p.add_argument('--dir',default=str(ROOT))
    p.add_argument('--out',default=str(ROOT/'pc/build/tests/enemy_difficulty'))
    p.add_argument('--port',type=int)
    p.add_argument('--fast',action='store_true')
    args=p.parse_args()
    os.environ['ROOD_SYMBOLS']=str(pathlib.Path(args.exe).resolve())
    print('Starting temporary card test',flush=True)
    t=Run(args)
    def warp(zone,room):
        request=t.game.call('room_goto',zone=zone,room=room,enemies=True,scripts=False,block_saves=True)
        assert request['ok'],request
        # A loaded save may open a workshop description before field control.
        for _ in range(120):
            if t.game.call('room_goto',action='status')['phase']!=1: break
            t.game.press('circle',vsyncs=6,settle=24)
        assert t.wait_for(lambda s:s['zone']==zone and s['room']==room and s['player'] is not None,2400)
        assert t.warp_settled() is not None
    try:
        print('Waiting for title',flush=True)
        t.game.wait_title(timeout=120)
        t.game.pause()
        t.game.call('graphics',scale=1)
        t.game.call('button_roles',confirm_cross=False,symbol_exceptions=False)
        t.game.call('settings',apply=['difficulty_base_percent 100','difficulty_clear_percent 0'])
        print('Loading disposable card',flush=True)
        for _ in range(30):
            t.game.press('circle',vsyncs=6,settle=54)
            state=t.state()
            if state['player'] is not None and state['zone'] not in (0,20): break
        assert t.wait_for(lambda s:s['player'] is not None and s['zone'] not in (0,20),1800)
        original_player=next(a for a in actors(t.game) if a['slot']==0)
        print('Loaded field; moving to test rooms',flush=True)
        warp(9,1)
        t.game.call('state_flag',id=PROGRESS_FLAG,value=3)
        warp(9,17)
        live=actors(t.game)
        print('spawned:',live,flush=True)
        t.check(t.game.u32('pc_difficulty_active_base')==100,'100 percent latched')
        player=next(a for a in live if a['slot']==0)
        companion=next(a for a in live if a['model']==8)
        t.check(player['main'][1]==original_player['main'][1],'Ashley maximum HP unchanged')
        t.check(companion['main'][1]==player['main'][1],'Merlose maximum HP matches unscaled Ashley')
        enemies=[a for a in live if a['slot'] and a['model']!=8]
        t.check(bool(enemies),'enemies present')
        for a in enemies:
            values=[a['main'][i] for i in (1,3,5,6,7,8,9,10)]
            t.check(all(v==999 or v%2==0 for v in values),'main stats doubled or capped')
        t.game.call('settings',apply='difficulty_base_percent 17')
        t.check(t.game.u32('pc_difficulty_active_base')==100,'setting change waits for cache reset')
        target=enemies[0]
        injured=max(1,target['hp']-7)
        t.game.write(target['address']+0x18,struct.pack('<H',injured))
        limb=next((i for i,v in enumerate(target['limbs']) if v[0]>10),None)
        if limb is not None:
            t.game.write(target['address']+0x398+limb*0xdc,struct.pack('<h',target['limbs'][limb][0]-7))
        warp(9,1)
        warp(9,17)
        current=next(a for a in actors(t.game) if a['model']==target['model'] and a['spawn']==target['spawn'])
        t.check(current['main'][1]==target['main'][1],'revisit does not change maximum HP')
        t.check(current['hp']<=injured,'revisit preserves damage')
        t.check([x[1] for x in current['limbs']]==[x[1] for x in target['limbs']],'limb maxima survive revisit')
        t.check(t.game.u32('pc_difficulty_active_base')==100,'same-scene revisit retains active multiplier')
        cache=t.game.read_symbol('D_80061078',4*328)
        found=False
        for area in range(4):
            offset=area*328
            if cache[offset+1]!=9 or struct.unpack_from('<H',cache,offset+2)[0]!=17: continue
            for index in range(8):
                rec=offset+8+index*40
                if cache[rec]==target['spawn']+1:
                    raw_str=struct.unpack_from('<h',cache,rec+12)[0]
                    t.check(raw_str*2==target['main'][6],'card-bound cache contains original STR')
                    found=True
        t.check(found,'target found in vanilla room cache')
        t.game.call('settings',apply=['difficulty_base_percent 30','difficulty_clear_percent 30'])
        t.game.write(rood_rpc.symbols()['vs_main_stateFlags'],bytes([2]))
        rooms=t.game.call('rooms')['rooms']
        current_scene=next(r['scene'] for r in rooms if r['zone']==9 and r['room']==17)
        destination=next(r for r in rooms if r['zone']!=9 and r['entries']>0 and r['scene']!=current_scene)
        print('completion-test destination:',destination,flush=True)
        warp(destination['zone'],destination['room'])
        t.check(t.game.u32('pc_difficulty_active_base')==30 and t.game.u32('pc_difficulty_active_clear')==30 and
            t.game.u32('pc_difficulty_active_completions')==2,'third playthrough: 30 + 2*30 = 90 percent')
        t.game.call('snapshot',action='mark')
        t.game.call('settings',apply=['difficulty_base_percent 17','difficulty_clear_percent 0'])
        warp(9,17)
        t.check(t.game.u32('pc_difficulty_active_base')==17,'new scene applies changed difficulty')
        t.game.call('snapshot',action='rewind')
        t.check(t.game.u32('pc_difficulty_active_base')==30,'snapshot restores active difficulty')
        t.check(not t.log_problems(),'no crash or unimplemented stops')
        t.shot('enemy-difficulty')
    finally:
        t.close()
    print('PASS' if not t.failures else 'FAIL',flush=True)
    return bool(t.failures)

if __name__=='__main__':
    raise SystemExit(main())
