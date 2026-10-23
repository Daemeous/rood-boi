#!/usr/bin/env python3
"""The story's scenes in story order, for the cutscene viewer
(pc/docs/Cutscene_Player.md, pc/docs/plans/2026-10-22-cutscene-viewer.md).

    python3 pc/tools/story_scan.py [--data data] [--disc DISC] [--json OUT] [--apply] [--trace V]

How the game starts a scene (BATTLE.PRG 4A0A8.c, 146C.c):
  - entering a room runs the room's own script (the MPD's script section,
    event source 1) when it has one (func_800BEC58(1)); otherwise its
    trigger table's entry triggers;
  - the trigger table (the MPD's "cleared" section, up to 16 entries:
    type mask, event id, conditions) starts an event when Ashley steps on a
    cell or region (type 2), examines something (4), after a kill (8), after
    a battle (16), each field frame (32); conditions test her cell, story
    flags, items, actors (func_800BEC58); the event id is an EVT file
    (EVENT/nnnn.EVT, source 0) or, bit 15 set, a script of the room's door
    section (source 2);
  - a script may chain to an EVT file when it ends (op 0xF7, func_800BB5AC)
    and move to another room (op 0x68, func_800B98C0).
  - story progress is state flag 130 (vs_main_stateFlags byte 0x82): room
    scripts test it (op 0x40) and each story scene sets it to the next step
    (op 0x50 mode 0x70). Flags 16-31 are cleared per event, 32-63 per room
    load, 64-127 per zone load; 128-1023 persist (the save's).

This walks the scripts concretely: from a new game's flags (all 0), for
story step v = 0, 1, ...: every room script and every trigger whose flag
conditions hold is walked with the flags as they are (op lengths: BATTLE.PRG
D_800E9C30; flag ops as the interpreter does them; loops and threads
bounded). The scripts that move flag 130 on from v are the scene(s) of
step v; their persistent writes (flags >= 128) make the flags of the next
step. Each scene gets: the room it plays in (where its script is), how it
starts (entry / trigger), the event id ((source << 12) | file), the flags
"just before" (every persistent flag that differs from a new game), the
dialogue boxes, chained EVT files and rooms it moves to.

The result names files by number and flags by index only (no game text).
--json writes it; --apply merges it into pc/assets/cutscenes/story.json,
keeping the human names, chapters and verification notes there by scene id.
"""
import argparse
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))

VRAM = 0x80068800       # BATTLE.PRG's load address (config/BATTLE/BATTLE.PRG/splat.yaml)
OP_LENGTHS = 0x800E9C30  # D_800E9C30 (573B8.c func_800BFE34)
STORY = 130             # the story step flag
PERSIST = 128           # flags from here on are kept (saved); below are per event / room / zone
COND_LEN = {1: 3, 2: 5, 3: 4, 15: 4, 4: 3, 5: 4, 6: 5, 7: 5, 8: 3, 9: 1, 10: 5, 11: 6, 12: 5, 13: 5, 14: 7,
            16: 3, 0xFE: 1}  # func_800BEC58's trigger conditions
DEBUG_ZONES = 250       # zones 250 and up: the developers' test rooms (their events jump the story about)
DEBUG_EVENTS = 504      # EVT 504 and up: those rooms' story jumps
STORY_JSON = ROOT / "pc/assets/cutscenes/story.json"


def s8(v):
    v &= 0xFF
    return v - 256 if v > 127 else v


def compare(mode, left, right):
    """The interpreter's flag compare (jumpFwdIfFlag: true = fall through)."""
    return {0x00: left <= right, 0x10: left >= right, 0x20: left == right, 0x30: left != right,
            0x40: left < right, 0x50: left > right, 0x60: bool(left & right)}.get(mode, False)


def arith(mode, old, val):
    if mode == 0x00:
        return s8(old + val)
    if mode == 0x10:
        return s8(old - val)
    if mode == 0x20:
        return s8(old * val)
    if mode == 0x30:
        return s8(int(old / val)) if val else old
    if mode == 0x40:
        return s8(old % val) if val else old
    if mode == 0x50:
        return s8(old & val)
    if mode == 0x60:
        return s8(old | val)
    if mode == 0x70:
        return s8(val)
    return old


class Segment:
    """One script run: a room's entry script, a trigger's event, or an EVT
    file a script chained to when it ended."""

    def __init__(self, kind, event, room, flags, sound):
        self.kind = kind  # entry / trigger / chain
        self.event = event  # (source << 12) | file
        self.room = room  # (zone, room) where it starts
        self.flags = {k: v for k, v in sorted(flags.items()) if k >= PERSIST and v}
        self.zone_flags = {k: v for k, v in sorted(flags.items()) if k >= 64 and v}  # (a handover's: zone flags too)
        self.sound = [sound.get(1, 0), sound.get(2, 0), sound.get(3, 0), sound.get("cur", 0)]
        self.texts = 0
        self.story = []  # values written to flag 130
        self.moves = []  # rooms it goes to
        self.ops = 0


class Trace:
    def __init__(self):
        self.segments = []
        self.writes = []  # (flag, value) in order
        self.ops = 0
        self.ended = "end"

    @property
    def texts(self):
        return sum(s.texts for s in self.segments)

    @property
    def story(self):
        return [v for s in self.segments for v in s.story]

    @property
    def chains(self):
        return [s.event for s in self.segments if s.kind == "chain"]

    @property
    def rooms(self):
        return [m for s in self.segments for m in s.moves]


ROOMS = {}  # (zone, room) -> its sections (load() fills it): a script's room change runs the new room's script


def walk(code, flags, lengths, evts, trace=None, depth=0, kind="entry", event=0x1000, room=(0, 0), sound=None):
    """Run a script concretely on `flags` and `sound` (the sound effect sets loaded: slot -> SE file,
    "cur" -> the current slot; dicts, changed in place)."""
    trace = trace or Trace()
    sound = {} if sound is None else sound
    seg = Segment(kind, event, room, flags, sound)
    trace.segments.append(seg)
    if len(code) < 16:
        return trace
    end = struct.unpack_from("<H", code, 2)[0]
    if end < 16 or end > len(code):
        end = len(code)
    threads = [16]
    counters = {}
    context = 0
    chain = None
    handover = None  # a room change that ends the script: the new room's entry script runs next
    while threads:
        p = threads.pop(0)
        visits = {}
        while 16 <= p < end:
            visits[p] = visits.get(p, 0) + 1
            if visits[p] > 64 or trace.ops > 60000:
                trace.ended = "loop"
                break
            op = code[p]
            n = lengths[op]
            if n == 0 or p + n > len(code):
                trace.ended = f"bad op {op:#x} at {p:#x}"
                break
            b = code[p:p + n]
            trace.ops += 1
            seg.ops += 1
            nxt = p + n
            if op == 0x40:
                f = (b[2] >> 2) | ((b[1] & 0xF) << 6)
                if not compare(b[1] & 0xF0, flags.get(f, 0), s8(b[3])):
                    nxt = p + (b[4] | b[5] << 8)
            elif op == 0x41:
                f0, f1 = (b[2] >> 2) | ((b[1] & 0xF) << 6), b[3] | ((b[2] & 3) << 8)
                if not compare(b[1] & 0xF0, flags.get(f0, 0), flags.get(f1, 0)):
                    nxt = p + (b[4] | b[5] << 8)
            elif op in (0x42, 0x45, 0x4B, 0x4C):
                nxt = p + (b[1] | b[2] << 8)
            elif op == 0x46:
                context = flags.get(b[1] | b[2] << 8, 0)
            elif op == 0x47:
                if s8(context) == s8(b[1]):
                    nxt = p + (b[2] | b[3] << 8)
            elif op == 0x49:
                if b[1] != 0xFF:
                    counters[b[1]] = b[2]
            elif op == 0x4A:
                back = b[2] | b[3] << 8
                if b[1] == 0xFF:
                    trace.ended = "loop"
                    break
                counters[b[1]] = counters.get(b[1], 1) - 1
                if counters[b[1]] > 0:
                    nxt = p - back
            elif op == 0x50:
                f = (b[2] >> 2) | ((b[1] & 0xF) << 6)
                flags[f] = arith(b[1] & 0xF0, flags.get(f, 0), s8(b[3]))
                trace.writes.append((f, flags[f]))
                if f == STORY:
                    seg.story.append(flags[f])
            elif op == 0x51:
                f0, f1 = (b[2] >> 2) | ((b[1] & 0xF) << 6), b[3] | ((b[2] & 3) << 8)
                mode = b[1] & 0xF0
                if mode == 0x90:
                    flags[f0], flags[f1] = flags.get(f1, 0), flags.get(f0, 0)
                    trace.writes += [(f0, flags[f0]), (f1, flags[f1])]
                else:
                    flags[f0] = arith(mode if mode != 0x80 else 0x70, flags.get(f0, 0), flags.get(f1, 0))
                    trace.writes.append((f0, flags[f0]))
                if f0 == STORY:
                    seg.story.append(flags[f0])
            elif op == 0x10:
                seg.texts += 1
            elif op == 0xF5:
                threads.append(p + 4)
                nxt = p + (b[2] | b[3] << 8)
            elif op in (0xF6, 0xFE, 0xFF):
                break
            elif op == 0xF7:
                chain = b[1] | b[2] << 8
            elif op == 0x68:  # func_800B98C0 [op, zone (0: this one), room, ..., b[7] bit 2: the script goes on]
                room = (b[1] or room[0], b[2])
                seg.moves.append(room)
                # without bit 2 the new room's entry script takes over (the event ends as it loads). Scripts
                # name several rooms in a row (the prologue's 1/0: 8/0 then 2/0); the walk keeps the last,
                # which is where the story goes on (the game's runs agree: story_verify.py)
                handover = None if b[7] & 4 else room
            elif op == 0x85 and 1 <= b[2] <= 3:  # loadSfxSlot [op, SE file, slot]
                sound.setdefault(b[2], b[1])
            elif op == 0x87 and 1 <= b[1] <= 3:  # freeSfx [op, slot]
                sound.pop(b[1], None)
                if sound.get("cur") == b[1]:
                    sound["cur"] = 0
            elif op == 0x88 and b[1] in sound:  # setCurrentSfx [op, slot]
                sound["cur"] = b[1]
            p = nxt
    if chain is not None and depth < 8 and chain in evts:
        walk(evts[chain], flags, lengths, evts, trace, depth + 1, "chain", chain, room, sound)
    elif handover is not None and depth < 8 and ROOMS.get(handover, {}).get("script"):
        for k in list(flags):
            if 32 <= k < 64 or (64 <= k < 128 and handover[0] != seg.room[0]):
                flags.pop(k)
        walk(ROOMS[handover]["script"], flags, lengths, evts, trace, depth + 1, "move", 0x1000, handover, sound)
    return trace


def trigger_holds(conds, flags):
    """A trigger's flag conditions (types 3 and 15) on these flags; the
    others (cells, actors, items) are taken as met."""
    negate = 0
    for c in conds:
        if c[0] == 0xFE:
            negate = 1
            continue
        if c[0] in (3, 15):
            left = flags.get(((c[1] & 0xF) << 6) | (c[2] >> 2), 0)
            right = s8(c[3]) if c[0] == 3 else flags.get(((c[2] & 3) << 8) | c[3], 0)
            op = c[1] & 0xF0
            # the trigger compare (func_800BEC58): 0 <=, 0x10 >=, 0x20 ==, 0x30 !=, 0x40 <, 0x50 >
            result = compare(op, left, right) if op != 0x60 else False
            if result == bool(negate):
                return False
        negate = 0
    return True


def load(data, disc_path):
    import fixture_lib as F
    disc = F.Disc(disc_path)
    prg = (data / "BATTLE/BATTLE.PRG").read_bytes()
    lengths = prg[OP_LENGTHS - VRAM:OP_LENGTHS - VRAM + 256]
    if (lengths[0x40], lengths[0x41], lengths[0x46], lengths[0x50], lengths[0xF7]) != (6, 6, 3, 4, 3):
        sys.exit(f"BATTLE.PRG: no op length table at {OP_LENGTHS:#x}")
    evts = {}
    for f in sorted((data / "EVENT").glob("*.EVT")):
        b = f.read_bytes()
        if any(b[16:64]) and int(f.stem) < DEBUG_EVENTS:
            evts[int(f.stem)] = b
    rooms = {}
    for z in disc.zones():
        if z == 0 or z >= DEBUG_ZONES:
            continue
        _, names, _ = F.zone_rooms(disc, z)
        for r, name in enumerate(names):
            if not name:
                continue
            b = disc.read("MAP/" + name)
            co, cl = struct.unpack_from("<I", b, 8)[0], struct.unpack_from("<H", b, 12)[0]
            so, sl = struct.unpack_from("<I", b, 16)[0], struct.unpack_from("<H", b, 20)[0]
            do, dl = struct.unpack_from("<I", b, 24)[0], struct.unpack_from("<H", b, 28)[0]
            room = {"script": b[so:so + sl] if sl else None, "triggers": [], "doors": b[do:do + dl] if dl else b""}
            t = b[co:co + cl] if cl else b""
            for i in range(16 if t else 0):
                off = struct.unpack_from("<H", t, 2 * i)[0]
                if off == 0xFFFF:
                    break
                p = off
                typ, ev = t[p], struct.unpack_from("<H", t, p + 1)[0]
                p += 3
                conds = []
                while t[p] != 0xFF:
                    n = COND_LEN.get(t[p])
                    if n is None:
                        break
                    conds.append(tuple(t[p:p + n]))
                    p += n
                room["triggers"].append({"type": typ, "event": ev, "conds": conds, "index": i})
            rooms[(z, r)] = room
    ROOMS.update(rooms)
    return lengths, evts, rooms


def door_script(room, index):
    d = room["doors"]
    if not d or 2 * index + 2 > len(d):
        return None
    off = struct.unpack_from("<h", d, 2 * index)[0]
    return d[off:] if 0 < off < len(d) else None


def candidates(flags, sound, lengths, evts, rooms):
    """Every script that would run now: (zone, room, trace, flags after, sound after). A room's triggers
    are tested on the flags its entry script left (they set room flags 32-63 the triggers test). Each
    trace keeps what the game is booted with to reach it (boot_flags, boot_sound: before the room's entry
    script, which runs on the way)."""
    out = []
    for (z, r), room in sorted(rooms.items()):
        entered = dict(flags)
        for k in range(32, 64):
            entered.pop(k, None)
        heard = dict(sound)
        boot = ({k: v for k, v in sorted(entered.items()) if k >= PERSIST and v}, Segment("", 0, (0, 0), {}, sound).sound)
        if room["script"]:
            f, snd = dict(entered), dict(sound)
            t = walk(room["script"], f, lengths, evts, kind="entry", event=0x1000, room=(z, r), sound=snd)
            t.boot = boot
            out.append((z, r, t, f, snd))
            entered = {k: v for k, v in f.items() if k != STORY or v == flags.get(STORY)}
            entered[STORY] = flags.get(STORY, 0)
            heard = dict(snd)
        for trig in room["triggers"]:
            if not trigger_holds(trig["conds"], entered):
                continue
            ev = trig["event"]
            if ev & 0x8000:
                code = door_script(room, ev & 0xF)
                eid = 0x2000 | (ev & 0xF)
            else:
                code = evts.get(ev)
                eid = ev
            if code is None:
                continue
            f, snd = dict(entered), dict(heard)
            t = walk(code, f, lengths, evts, kind="trigger", event=eid, room=(z, r), sound=snd)
            t.trigger = {"type": trig["type"], "index": trig["index"]}
            t.boot = boot
            out.append((z, r, t, f, snd))
    return out


def scene_id(seg, step):
    z, r = seg.room
    src, file = seg.event >> 12, seg.event & 0xFFF
    if src == 0:
        return f"evt-{file:04d}"
    if src == 1:
        return f"room-{z:03d}-{r:02d}-s{step:03d}"
    return f"door-{z:03d}-{r:02d}-{file}-s{step:03d}"


def record(scenes, seen, trace, step, part):
    """The trace's segments as scenes. Each is reached by booting into the trace's first room with the
    flags and sounds from before it (its entry script runs), starting the trigger's event when the trace
    is a trigger's, and, for a segment after the first (an EVT file chained to), playing on until that
    event runs."""
    root = trace.segments[0]
    boot_flags, boot_sound = getattr(trace, "boot", (root.flags, root.sound))
    for i, seg in enumerate(trace.segments):
        if seg.kind == "move":  # the scenes after it start from here (the nearest way in)
            root, boot_flags, boot_sound = seg, seg.zone_flags, seg.sound
        at = seg.flags.get(STORY, 0)
        sid = scene_id(seg, at)
        if sid in seen:
            continue
        seen.add(sid)
        entry = {"id": sid, "step": at, "part": part, "plays_in": list(seg.room), "event": seg.event,
                 "parent": scene_id(trace.segments[i - 1], trace.segments[i - 1].flags.get(STORY, 0)) if i else None,
                 "texts": seg.texts, "sets_step": seg.story[-1] if seg.story else None,
                 "moves": [list(m) for m in seg.moves], "ops": seg.ops, "trigger": None, "until_room": None}
        if seg is root and seg.kind == "move":
            # a room handed over to: the game's own way in, its entry script with the flags (zone flags
            # too: the hand-over marks, 170 / 171, and the like) as the script before left them
            entry.update(zone=seg.room[0], room=seg.room[1], start="entry", force=-1, until=-1,
                         flags={str(k): v for k, v in seg.zone_flags.items()}, sound=seg.sound)
        else:
            # the first script, or an EVT file chained to: from the first script's room and flags
            entry.update(zone=root.room[0], room=root.room[1], start="chain" if seg is not root else root.kind,
                         force=root.event if root.kind == "trigger" else -1,
                         until=seg.event if seg is not root else -1,
                         until_room=list(seg.room) if seg is not root else None,
                         trigger=getattr(trace, "trigger", None) if root.kind == "trigger" else None,
                         flags={str(k): v for k, v in boot_flags.items()}, sound=boot_sound)
        scenes.append(entry)


def prologue(lengths, evts, rooms, scenes, seen):
    """New Game: zone 1 room 0, and the rooms its scripts move on to (each
    script's last move), until the story step flag is first set."""
    flags, sound = {}, {}
    cur, zone = (1, 0), None
    for _ in range(32):
        room = rooms.get(cur)
        if room is None or not room["script"]:
            break
        for k in list(flags):
            if 32 <= k < 64 or (64 <= k < 128 and cur[0] != zone):
                flags.pop(k)
        zone = cur[0]
        t = walk(room["script"], flags, lengths, evts, kind="entry", event=0x1000, room=cur, sound=sound)
        record(scenes, seen, t, 0, "prologue")
        if flags.get(STORY) or not t.rooms:
            break
        cur = t.rooms[-1]
    return flags, sound


def scan(lengths, evts, rooms, trace_step=None):
    scenes, seen = [], set()
    flags, sound = prologue(lengths, evts, rooms, scenes, seen)
    step = flags.get(STORY, 0)
    stuck = []
    while step < 128:
        flags[STORY] = step
        cands = candidates(flags, sound, lengths, evts, rooms)
        movers = [c for c in cands if c[2].story and c[3].get(STORY, step) > step]
        if trace_step is not None and step == trace_step:
            for z, r, t, f, snd in cands:
                if t.texts or t.story or t.chains:
                    print(z, r, [hex(sg.event) for sg in t.segments], t.texts, t.story, t.rooms[:4], t.ended,
                          file=sys.stderr)
        if not movers:
            stuck.append(step)
            step += 1
            continue
        # the scene that moves the story least far is the next one (others are later ones that
        # also accept this step: "130 <= v" gates)
        movers.sort(key=lambda c: (c[3][STORY], -c[2].texts))
        nxt = movers[0][3][STORY]
        for z, r, t, after, snd in movers:
            if after[STORY] == nxt:
                record(scenes, seen, t, step, "story")
                for k, v in after.items():
                    if k >= PERSIST:
                        flags[k] = v
                sound = dict(snd)
        step = nxt
    return scenes, stuck


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=str(ROOT / "data"))
    ap.add_argument("--disc")
    ap.add_argument("--json")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--trace", type=int, help="print every script with content at this step (stderr)")
    args = ap.parse_args()
    lengths, evts, rooms = load(pathlib.Path(args.data), args.disc)
    scenes, stuck = scan(lengths, evts, rooms, args.trace)
    for s in scenes:
        print(f"{s['step']:3d} -> {str(s['sets_step']):4s} {s['start']:7s} {s['zone']:3d}/{s['room']:<3d} "
              f"ev {s['event']:#06x} texts {s['texts']:3d} sound {s['sound']} {s['id']}")
    print(f"{len(scenes)} scenes; steps with no scene: {stuck}")
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps({"scenes": scenes, "stuck": stuck}, indent=1) + "\n")
    if args.apply:
        apply(scenes)


# The parts of the story the viewer groups scenes under (our names), by story step: the first whose
# step range holds the scene's step (the prologue: the scenes before step 0's are set).
CHAPTERS = [
    (None, None, "Prologue"),
    (0, 11, "The Wine Cellar"),
    (12, 20, "The Catacombs and the Sanctum"),
    (21, 37, "The Town Center"),
    (38, 46, "The Snowfly Forest"),
    (47, 54, "The Keep"),
    (55, 73, "The Undercity and the Mines"),
    (74, 74, "The Temple of Kiltia"),
    (75, 999, "The Great Cathedral"),
]


def chapter_of(scene):
    if scene["part"] == "prologue" and scene["sets_step"] is None:
        return CHAPTERS[0][2]
    for lo, hi, name in CHAPTERS[1:]:
        if lo <= scene["step"] <= hi:
            return name
    return "Story"


KEEP = ("name", "chapter", "major", "verified", "frames", "notes", "entry", "cell", "skip")
ABOUT = ("The story's scenes in story order for the cutscene viewer (pc/src/cutscene.cpp, "
         "pc/docs/Cutscene_Player.md). Generated by pc/tools/story_scan.py --apply from the game's scripts "
         "(ids, rooms, event ids and flag indices only); name, chapter, major, verified, frames, notes, entry, "
         "cell and skip are kept by id across rescans (skip: listed here, not shown: a script with nothing to "
         "watch, story_verify.py). The game is started in zone/room (its entry script runs) "
         "with flags (the story flags just before: index: value, every other flag 0) and sound (the sound "
         "effect sets loaded: SE files in slots 1-3, the current slot); then force: an event started as its "
         "trigger would (-1 none); until: the event to play on to (a chained EVT file; -1: the take starts "
         "at once). start: entry, trigger or chain. step: flag 130, the story step. event: the scene's own "
         "event id ((source << 12) | file). plays_in: the room it starts in.")


def apply(scenes):
    old = {}
    if STORY_JSON.exists():
        for e in json.loads(STORY_JSON.read_text()).get("scenes", []):
            old[e["id"]] = e
    out = []
    for s in scenes:
        e = {"id": s["id"], "name": "", "chapter": "", "step": s["step"], "zone": s["zone"], "room": s["room"],
             "plays_in": s["plays_in"], "start": s["start"], "event": s["event"], "force": s["force"],
             "until": s["until"], "until_room": s["until_room"], "parent": s["parent"], "texts": s["texts"], "sets_step": s["sets_step"],
             "sound": s["sound"], "flags": s["flags"]}
        for k in KEEP:
            if k in old.get(s["id"], {}):
                e[k] = old[s["id"]][k]
        if not e["name"]:
            e["name"] = f"Step {s['step']}: zone {s['zone']} room {s['room']} ({s['id']})"
        if not e["chapter"] or e["chapter"] in ("Story", "Prologue: the Duke's manor", "Into Lea Monde"):
            e["chapter"] = chapter_of(s)
        out.append(e)
    text = json.dumps({"about": ABOUT, "scenes": out}, indent=1)
    # one scene per block, its flags on one line
    STORY_JSON.write_text(text + "\n")
    print(f"{STORY_JSON}: {len(out)} scenes")


if __name__ == "__main__":
    main()
