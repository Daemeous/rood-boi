#!/usr/bin/env python3
"""The player state API (pc/src/hud/player_bridge.c, pc/src/lua/lua_api_player.cpp;
pc/docs/Extensions.md "Player state"): rood.game.inventory / weapons / shields /
armour / accessories / equipment / workshop / combine / craftable and RPC
game_state / combine_preview.

    python3 pc/tests/player_state_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

One game from a copy of the test card (the save room), the original menus
(ui_menus off):
  1. the queries against the game's records (vs_main_inventory and its lists,
     read over RPC: counts per category, each weapon's blade, grip and gems,
     what is equipped where, armor worn, misc ids and counts, keys and sigils
     by id) and against what the modern Items screen lists (its names, sub
     lines and counts per category: ui_menu items, the tab opened and closed
     again); a query changes nothing (RPC snapshot hash before and after);
  2. Lua: the same values as the RPC, read-only tables, the same table for a
     query within a vsync and a new one after the next vsync;
  3. ui_workshop_smoke's set-up (gems, damage) and a warp into a workshop
     (zone 47 room 0: every material): workshop(), craftable() (every
     ordered pair the Combine screen lists, Lua and RPC alike), then
     combine_preview of two blades and of two unworn armor pieces: the state
     hash unchanged by each preview, then the same combine done for real on
     the original Combine screen (MENUC) in this process, the stored record
     byte for byte the preview's `raw`, and the bag's description of it the
     preview's result;
  4. the example plugin rood.craftbook turned on at the workshop: its panel
     lists the craftable combinations (RPC hud stats: the panel's commands) in
     the classic look and the modern one; its Lua tests.
Skips without a card that resumes in the save room with two blades and two
unworn armor pieces."""
import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from ui_workshop_smoke import (ARMOR, ARMOR_REC, BLADE, BLADES, Session, WORKSHOP,  # noqa: E402
                               u16)

MISC = 0xE00
CATEGORIES = ["weapons", "blades", "grips", "shields", "armor", "gems", "misc"]
SLOTS = {1: "right_arm", 2: "left_arm", 3: "head", 4: "body", 5: "legs", 7: "accessory"}
KEY, SIGIL = 458, 466  # the game's first key and sigil ids (main.h itemIdentifiers)
SAME_FIELDS = ["kind", "id", "name", "material", "category", "dp", "dp_max", "pp", "pp_max", "str", "int", "agl",
               "classes", "affinities", "types", "damage_type", "range", "risk", "two_handed", "gem_slots"]


def hashes(g):
    h = g.call("snapshot", action="hash")
    return {k: h[k] for k in ("ram", "scratch", "io", "state", "overlays", "vram")}


def lua(g, code):
    r = g.call("lua", action="eval", code=code)
    if not r.get("ok"):
        raise RuntimeError(f"lua: {code!r}: {r.get('error')}")
    return r["results"]


def full_order(bag, c, cap):
    """The menus' order: the list (records that exist), then records it lacks."""
    ids = {0: lambda i: bag.weapon(i)["blade"], 1: lambda i: bag.blade(i)["id"], 2: lambda i: bag.grip(i)["id"],
           3: lambda i: bag.shield(i)["id"], 4: lambda i: bag.armor(i)["id"], 5: lambda i: bag.gem(i)["id"],
           6: lambda i: u16(bag.inv, MISC + i * 4)}[c]
    order = [i for i in bag.listed(c) if i < cap and ids(i)]
    order += [i for i in range(cap) if ids(i) and i not in order]
    return order


def check_records(s, state, check):
    """The queries against the records (vs_main_inventory and its lists)."""
    bag = s.bag()
    caps = [8, 16, 16, 8, 16, 48, 64]
    inv = state["inventory"]
    orders = [full_order(bag, c, caps[c]) for c in range(7)]
    for c, name in enumerate(CATEGORIES):
        got = inv["counts"][name]
        check(got == {"count": len(orders[c]), "capacity": caps[c]},
              f"records: {name} count {got} (the bag has {len(orders[c])} of {caps[c]})")
    weapons = state["weapons"]
    check([w["record"] - 1 for w in weapons] == orders[0], f"records: the weapons in the menus' order "
          f"({[w['record'] for w in weapons]})")
    for w in weapons:
        rec = bag.weapon(w["record"] - 1)
        gems = [x - 1 for x in rec["gems"][:w["gem_slots"]] if x]
        check(w["blade"]["record"] == rec["blade"] and w.get("grip", {}).get("record", 0) == rec["grip"]
              and [x["record"] - 1 for x in w["gems"]] == gems and w["equipped"] == bool(rec["equipped"])
              and w["blade"]["weapon"] == w["record"],
              f"records: weapon {w['record']} {w['name']!r}: blade {w['blade']['record']}, grip "
              f"{w.get('grip', {}).get('record')}, gems {[x['record'] for x in w['gems']]}, equipped {w['equipped']}")
    for sh in state["shields"]:
        rec = bag.shield(sh["record"] - 1)
        check(sh["id"] == rec["id"] and sh["equipped"] == bool(rec["equipped"]),
              f"records: shield {sh['record']} {sh['name']!r} equipped {sh['equipped']}")
    worn = {}
    for a in state["armour"] + state["accessories"]:
        rec = bag.armor(a["record"] - 1)
        want_kind = "accessory" if rec["category"] == 7 else "armor"
        check(a["id"] == rec["id"] and a["kind"] == want_kind and a.get("slot") == SLOTS.get(rec["limb"]),
              f"records: {a['kind']} {a['record']} {a['name']!r} slot {a.get('slot')} (limb {rec['limb']})")
        if rec["limb"]:
            worn.setdefault(SLOTS[rec["limb"]], a["record"])
    eq = state["equipment"]
    for slot, record in worn.items():
        check(eq.get(slot, {}).get("record") == record, f"records: equipment {slot} is armor record {record}")
    weapon = next((w["record"] for w in weapons if w["equipped"]), None)
    check(eq.get("weapon", {}).get("record") == weapon, f"records: the equipped weapon is record {weapon}")
    misc = [(u16(bag.inv, MISC + i * 4), bag.inv[MISC + i * 4 + 2]) for i in orders[6]]
    listed = inv["items"] + inv["keys"] + inv["sigils"]
    got = sorted((x["id"], x["count"]) for x in listed)
    check(got == sorted(misc), f"records: misc ids and counts ({len(got)} entries)")
    check(all(x["id"] < KEY for x in inv["items"]) and all(KEY <= x["id"] < SIGIL for x in inv["keys"])
          and all(x["id"] >= SIGIL for x in inv["sigils"]),
          f"records: items / keys / sigils split by id ({len(inv['items'])}, {len(inv['keys'])}, "
          f"{len(inv['sigils'])})")


def check_menus(s, state, check):
    """The queries against what the modern Items screen lists."""
    g = s.g
    g.call("ui_menu", modern=True)
    for _ in range(6):
        if s.ui().get("visible"):
            break
        g.press("triangle", vsyncs=3, settle=4)
        s.wait(lambda: s.ui().get("visible"), 120, "", quiet=True)
    for _ in range(8):
        if s.ui().get("tab") == "items":
            break
        g.press("r1", vsyncs=3, settle=4)
    g.run(10)
    ours = {"weapons": [(w["name"], 0) for w in state["weapons"]],
            "blades": [(b["name"], 0) for b in state["inventory"]["blades"]],
            "grips": [(b["name"], 0) for b in state["inventory"]["grips"]],
            "shields": [(b["name"], 0) for b in state["shields"]],
            "gems": [(b["name"], 0) for b in state["inventory"]["gems"]]}
    seen = {}
    for k, cat in enumerate(CATEGORIES):
        for _ in range(9):
            it = s.ui().get("items") or {}
            if it.get("category") == cat:
                break
            g.press("r2", vsyncs=3, settle=4)
        it = s.ui().get("items") or {}
        if it.get("category") != cat:
            check(False, f"menus: the Items screen's {cat} list not reached")
            continue
        seen[cat] = [(e["name"], e.get("count", 0)) for e in it.get("list", [])]
        if k == 4:  # armor and accessories together, in one list
            names = {a["record"]: a["name"] for a in state["armour"] + state["accessories"]}
            order = [e["inventory"] + 1 for e in it.get("list", [])]
            check([names.get(r) for r in order] == [n for n, _ in seen[cat]],
                  f"menus: armor names in the screen's order ({len(order)})")
        elif k == 6:
            misc = {x["record"]: (x["name"], x["count"]) for x in
                    state["inventory"]["items"] + state["inventory"]["keys"] + state["inventory"]["sigils"]}
            order = [e["inventory"] + 1 for e in it.get("list", [])]
            check([misc.get(r) for r in order] == seen[cat], f"menus: misc names and counts ({len(order)})")
        else:
            check(ours[cat] == [(n, 0) for n, _ in seen[cat]],
                  f"menus: {cat} names in order {[n for n, _ in seen[cat]]} (ours {[n for n, _ in ours[cat]]})")
    for _ in range(6):
        if not s.ui().get("visible"):
            break
        g.press("triangle", vsyncs=3, settle=4)
        s.wait(lambda: not s.ui().get("visible"), 120, "", quiet=True)
    g.run(30)
    g.call("ui_menu", modern=False)


def check_lua(s, check):
    g = s.g
    g.call("pause")
    same = lua(g, "local a, b = rood.game.weapons(), rood.game.weapons(); P_W = a; P_I = rood.game.inventory(); "
                  "return a == b, rood.game.inventory() == P_I, #a, a[1] and a[1].name")
    check(same[0] is True and same[1] is True, f"lua: the same table twice within a vsync {same}")
    again = lua(g, "return rood.game.weapons() == P_W, rood.game.inventory() == P_I")
    check(again == [True, True], f"lua: still the same tables while paused (no vsync) {again}")
    g.call("step", vsyncs=1)
    g.call("wait", timeout_ms=5000)
    after = lua(g, "local w = rood.game.weapons(); return w == P_W, w[1].name == P_W[1].name")
    check(after == [False, True], f"lua: a new table after a vsync, the same values {after}")
    ro = lua(g, "local w = rood.game.weapons(); local ok = pcall(function() w[1].name = 'x' end); "
                "local ok2 = pcall(function() w[1].blade.dp = 1 end); return ok, ok2, w[1].name")
    check(ro[0] is False and ro[1] is False, f"lua: read-only, every level {ro}")
    state = g.call("game_state", parts=["weapons", "inventory", "equipment"])
    w = state["weapons"][0]
    got = lua(g, "local w = rood.game.weapons()[1]; local i = rood.game.inventory(); local e = rood.game.equipment(); "
                 "return w.name, w.blade.name, w.blade.material, w.dp, w.classes.human, w.affinities.dark, "
                 "w.types.edged, #i.items, i.counts.misc.count, e.weapon and e.weapon.record")
    want = [w["name"], w["blade"]["name"], w["blade"].get("material"), w["dp"], w["classes"]["human"],
            w["affinities"]["dark"], w["types"]["edged"], len(state["inventory"]["items"]),
            state["inventory"]["counts"]["misc"]["count"], state["equipment"].get("weapon", {}).get("record")]
    check(got == want, f"lua: the RPC's values {got}")
    labels = g.call("game_state", parts=["labels"])["labels"]
    check([x.lower() for x in labels["classes"]] == ["human", "beast", "undead", "phantom", "dragon", "evil"]
          and [x.lower() for x in labels["affinities"]] == ["physical", "air", "fire", "earth", "water", "light", "dark"]
          and [x.lower() for x in labels["types"]] == ["blunt", "edged", "piercing"],
          f"the keys are the game's words in its order {labels}")
    g.call("pause", paused=False)


def combine_check(s, check, kind, a, b, category):
    """combine_preview of records a, b (0-based), the hash unchanged; then the real combine on
    the original Combine screen: the stored record is the preview's raw bytes."""
    g = s.g
    g.call("pause")
    before = hashes(g)
    p = g.call("combine_preview", kind=kind, a=a + 1, b=b + 1)
    lp = lua(g, f"local r = rood.game.combine({{kind = '{kind}', record = {a + 1}}}, "
                f"{{kind = '{kind}', record = {b + 1}}}); return r.result.name, r.result.dp_max, r.allowed")
    if kind == "blade":  # the queries' own (read-only) tables as arguments; a weapon stands for its blade
        lq = lua(g, "local by, wb = {}, {}; for _, x in ipairs(rood.game.inventory().blades) do by[x.record] = x end; "
                    "for _, w in ipairs(rood.game.weapons()) do wb[w.blade.record] = w end; "
                    f"local r, why = rood.game.combine(by[{a + 1}], by[{b + 1}]); "
                    f"local w = wb[{a + 1}] and wb[{b + 1}] and rood.game.combine(wb[{a + 1}], wb[{b + 1}]); "
                    "return r and r.result.name, why, w and w.result.name or r and r.result.name")
        check(lq == [p["result"]["name"], None, p["result"]["name"]],
              f"{kind}: combine takes the queries' tables (blades, weapons) {lq}")
    after = hashes(g)
    check(before == after, f"{kind}: the state hash is unchanged by the preview"
          + ("" if before == after else f" ({[k for k in before if before[k] != after[k]]})"))
    check(p["allowed"] and p["first"]["record"] == a + 1 and p["second"]["record"] == b + 1,
          f"{kind}: preview {p['first']['name']!r} + {p['second']['name']!r} -> {p['result']['name']!r} "
          f"{p['result'].get('material')} (allowed {p['allowed']})")
    check(lp == [p["result"]["name"], p["result"]["dp_max"], True], f"{kind}: Lua's combine is the RPC's {lp}")
    g.call("pause", paused=False)
    s.o_combine(category, a, b)
    size = BLADE if kind == "blade" else ARMOR_REC
    base = BLADES if kind == "blade" else ARMOR
    stored = s.inv()[base + a * size:base + (a + 1) * size].hex()
    check(stored == p["raw"], f"{kind}: the record the Combine screen stored is the preview's"
          + ("" if stored == p["raw"] else f" (stored {stored}, preview {p['raw']})"))
    state = g.call("game_state", parts=["inventory", "armour"])
    rows = state["inventory"]["blades"] if kind == "blade" else state["armour"]
    got = next((r for r in rows if r["record"] == a + 1), None)
    diff = [f for f in SAME_FIELDS if got is None or got.get(f) != p["result"].get(f)]
    check(not diff, f"{kind}: the bag's description of the stored record is the preview's result"
          + ("" if not diff else f" (differs: {diff}; {got} vs {p['result']})"))


def craftbook_check(s, check, crafts):
    """The example plugin (pc/plugins/rood.craftbook, off by default): turned on at the workshop it
    draws its panel with the craftable list, in the classic look and the modern one; its Lua tests."""
    g = s.g
    r = g.call("lua", action="enable", plugin="rood.craftbook", enabled=True)
    on = [p for p in r.get("plugins", []) if p["id"] == "rood.craftbook"]
    check(on and on[0]["state"] == "loaded", f"craftbook: enabled ({on and on[0]['state']})")
    first = crafts[0]
    want = f"{first['first']['name']} + {first['second']['name']}: {first['result']['name']}"
    for look in ("classic", "modern"):
        g.call("ui_menu", modern=look == "modern")
        g.run(40)
        g.wait_presents(4)
        text = json.dumps(g.call("hud", action="stats"))
        check(want in text and "Craftbook" in text, f"craftbook: its panel in the {look} look lists {want!r}")
        s.shot(f"craftbook-{look}")
    run = g.lua_test(plugin="rood.craftbook")
    check(run["failed"] == 0 and run["passed"] >= 3, f"craftbook: its Lua tests ({run['passed']} passed, "
          f"{run['failed']} failed{'' if not run['failed'] else ': ' + json.dumps(run['results'])})")
    g.call("lua", action="enable", plugin="rood.craftbook", enabled=False)
    g.call("ui_menu", modern=False)
    g.run(10)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "player_state").resolve()
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    if not os.environ.get("ROOD_OUT"):
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []
    t0 = time.monotonic()

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    s = Session("player", out, card, env, bool(args.out))
    try:
        g = s.g
        g.call("ui_menu", modern=False)
        st = g.state()
        if (st["zone"], st["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {st}")
            return 1
        g.call("pause")
        before = hashes(g)
        state = g.call("game_state")
        check(hashes(g) == before, "game_state changes nothing (state hash)")
        g.call("pause", paused=False)
        check(state["workshop"] is None, "workshop: none in the save room")
        check_records(s, state, check)
        check_menus(s, state, check)
        check_records(s, g.call("game_state"), check)  # the Items screen rebuilt the lists: still the records
        check_lua(s, check)

        s.prepare()
        bag = s.bag()
        blades = bag.listed(1)
        unworn = [i for i in bag.listed(4) if bag.armor(i)["limb"] == 0 and bag.armor(i)["category"] != 7]
        if len(blades) < 2 or len(unworn) < 2:
            print("SKIP: the card needs two blades and two unworn armor pieces")
            return 77
        s.warp(*WORKSHOP)
        ws = g.call("game_state", parts=["workshop", "craftable"])
        w = ws["workshop"]
        check(w is not None and w["number"] == 6 and len(w["materials"]) == 7 and w["name"],
              f"workshop: {w}")
        crafts = ws["craftable"]
        bag = s.bag()
        want = set()
        for kind, c in (("blade", 1), ("shield", 3), ("armor", 4)):
            rows = [i for i in bag.listed(c) if c != 4 or bag.armor(i)["category"] != 7]
            want |= {(kind, x + 1, y + 1) for x in rows for y in rows if x != y}
        got = {(e["kind"], e["first"]["record"], e["second"]["record"]) for e in crafts}
        check(got == want, f"craftable: every ordered pair the Combine screen lists ({len(got)} of {len(want)})")
        n = lua(g, "return #rood.game.craftable(), rood.game.workshop().number")
        check(n == [len(crafts), 6], f"craftable: Lua's list is the RPC's {n}")

        craftbook_check(s, check, crafts)
        combine_check(s, check, "blade", blades[0], blades[1], 1)
        combine_check(s, check, "armor", unworn[0], unworn[1], 4)
    finally:
        s.close()
    print(f"total {time.monotonic() - t0:.0f} s")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
