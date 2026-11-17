#!/usr/bin/env python3
"""The ENEMY menu's ZUD path (MENU9 592C.c): original classic menus, Bestiary, each foe focused in turn; prints what the preview
actor and the weapon slots hold after each load. Run on two builds (ROOD_BINARY, ROOD_SYMBOLS, ROOD_ARCH=64) and diff the output."""
import os, pathlib, sys, hashlib, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"pc/tools")); sys.path.insert(0, str(ROOT/"pc/tests"))
import ui_score_smoke as U
from card_fixtures import card_guard, default_save
out = pathlib.Path(tempfile.mkdtemp(prefix="bz-"))
env = {"ROOD_WINDOW": "960x720", "ROOD_GI": "0"}
os.environ.pop("ROOD_SETTINGS", None)
with card_guard(out/"cards", source=str(default_save())) as cards:
    a = U.Session("A", out, cards.card1, env, modern=False)
    try:
        g = a.game
        a.fixture("midgame", 1); a.open()
        row = g.value("vs_menu_frameworkMenuMemory.cursorMemories[2]")
        for _ in range((3 - row) & 3): a.step("down", None)
        a.step("circle", "bestiary")
        mb = g.data(a.m9["_monBinData"] + "[0]", 14336)
        mb = bytearray(mb)
        for i in range(78): mb[i*44+12] = 1
        g.store_data(a.m9["_monBinData"] + "[0]", bytes(mb))
        print("menu9 state", a.menu_state(), "menu", a.current_menu(), flush=True)
        for k in range(12):
            a.step("circle", None)
            g.run(400)
            print("after circle: menu9 state", a.menu_state(), "loadState", g.value("vs_battle_loadObjectDataState"), flush=True)
            rec = {}
            for p in ("unk0.nBones", "unk6E6", "unk5BB", "unk5B4", "unk5B5", "unk0.slot"):
                try: rec[p] = g.value(f"vs_actors_actorRecords[1]->{p}")
                except Exception as e: rec[p] = str(e)[:30]
            w = []
            for i in (2, 3):
                try:
                    w.append((g.value(f"vs_battle_wepModels[{i}]->nBones"), g.value(f"vs_battle_wepModels[{i}]->modelId")))
                except Exception as e:
                    w.append(str(e)[:20])
            print(k, rec, w, flush=True)
            a.step("cross", None)
            g.run(60)
            a.step("down", None)
            g.run(20)
    finally:
        a.close()
