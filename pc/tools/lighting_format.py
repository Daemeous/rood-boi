#!/usr/bin/env python3
"""Lighting pack files (format version 2): parse, format, merge, diff.

The Python side of pc/src/lighting_layers.cpp (the game's reader): the
lint (lighting_lint.py), tests and future generators use it. The schema
below must match the C++ one (pc/tests/lighting_layers_test.py compares
them through pc/build/lighting_probe --schema). Format: lighting/README.md.

    import lighting_format as lf
    room = lf.load_room(zone=9, room=17, root="lighting", packs=["default"], user="pc/user/lighting")
    room.base, room.effective, room.problems

A generator writes deterministic files with lf.format_file(entries) and
gives generated items ids from their source with lf.generated_id().
"""
import hashlib
import math
import os
import re

FORMAT_VERSION = 2

# kind -> (open: other keys allowed (an actor's prefab parameters),
#          decimals for other keys, [(key, decimals (-1 text), default or None, volatile)])
SCHEMA = {
    "actor": (True, 4, [
        ("name", -1, None, False), ("prefab", -1, None, False), ("pos", 1, None, False),
        ("rot", 2, "0,0,0", False), ("seed", 0, "1", False), ("enabled", 0, "1", False),
        ("glow", 1, None, True), ("glowkey", -1, None, False), ("fixture", 1, None, False),
        ("fixture_radius", 1, "16", False), ("fixture_shadows", 0, "0", False),
        ("fixture_type", -1, None, False), ("flame", 0, "0", False)]),
    # The room's sun ("env sun") and sky ("env sky"): pc/src/environment.cpp.
    "env": (False, 4, [
        ("enabled", 0, "1", False), ("azimuth", 2, "180", False), ("elevation", 2, "45", False),
        ("color", 3, "1,1,1", False), ("intensity", 3, "1", False), ("angle", 2, "0.53", False),
        ("shadows", 0, "1", False), ("zenith", 3, "0.1,0.12,0.2", False),
        ("horizon", 3, "0.25,0.25,0.3", False), ("ground", 3, "0.05,0.05,0.05", False),
        ("sun_tint", 3, "0", False), ("ambient", 3, "0.5", False),
        # The ground fog ("env fog"): pc/src/atmosphere.cpp.
        ("density", 5, "0.003", False), ("height", 1, "48", False), ("offset", 1, "0", False),
        ("noise", 2, "0.6", False), ("scale", 1, "160", False), ("wind", 1, "4,0,1.5", False),
        ("fill", 3, "0,0,0", False), ("sky", 3, "1", False)]),
    # Fog volumes ("fog <id>"): pc/src/atmosphere.cpp.
    "fog": (False, 4, [
        ("name", -1, None, False), ("shape", -1, "box", False), ("pos", 1, None, False),
        ("size", 1, "256,128,256", False), ("yaw", 2, "0", False), ("density", 5, "0.004", False),
        ("edge", 2, "0.5", False), ("color", 3, "1,1,1", False), ("fill", 3, "0,0,0", False),
        ("noise", 2, "0.5", False), ("enabled", 0, "1", False),
        # the interactive fog (pc/src/fog_sim.h): the characters, attacks and spells part it
        ("sim", 0, "0", False), ("sim_strength", 2, "1", False), ("sim_recover", 2, "3", False),
        ("sim_footprint", 2, "1", False), ("sim_trail", 2, "1", False), ("sim_wind", 2, "1", False),
        ("detail", 2, "0.5", False), ("sim_swirl", 2, "0", False), ("shadow", 2, "0", False),
        # each source's weight here (-1: the global setting fog_disturb_*)
        ("disturb_player", 2, "-1", False), ("disturb_actors", 2, "-1", False),
        ("disturb_attacks", 2, "-1", False), ("disturb_spells", 2, "-1", False),
        # a named preset (pc/assets/environment/fog_presets.json) for the fields not written
        ("preset", -1, None, False)]),
    "glow": (False, 4, [
        ("pos", 1, None, True), ("muted", 0, "0", False), ("intensity", 3, "1", False),
        ("tint", 3, "1,1,1", False), ("size", 2, "0", False), ("reach", 1, "0", False),
        ("fixture_type", -1, None, False), ("shape", -1, "auto", False)]),
    "light": (False, 4, [
        ("name", -1, None, False), ("type", -1, "point", False), ("pos", 1, None, False),
        ("dir", 4, "0,1,0", False), ("color", 3, "1,0.75,0.45", False), ("intensity", 3, "1", False),
        ("radius", 1, "600", False), ("cone", 2, "45", False), ("size", 1, "128,128", False),
        ("shadows", 0, "0", False), ("enabled", 0, "1", False)]),
    # Post-processing profiles ("post room", "post cutscene", "post event.<id>"): pc/src/post_profile.cpp.
    # Open: any post field (checked there), `preset` a named profile under them.
    "post": (True, 4, [("preset", -1, None, False)]),
    "set": (False, 6, [("value", 6, None, False)]),
    "uv": (False, 4, [("speed", 3, "1", False), ("freeze", 0, "0", False)]),
}
# What a define of each kind needs, and how many numbers a field takes.
REQUIRED = {"light": ["pos"], "actor": ["prefab", "pos"], "set": ["value"], "glow": [], "uv": [], "env": [],
            "fog": ["pos"], "post": []}
COUNTS = {"pos": 3, "dir": 3, "color": 3, "rot": 3, "glow": 3, "fixture": 3, "tint": 3, "size": None,
          "zenith": 3, "horizon": 3, "ground": 3, "wind": 3, "fill": 3}
# The env items a room may have, and the fields each takes.
ENV_FIELDS = {"sun": ("enabled", "azimuth", "elevation", "color", "intensity", "angle", "shadows"),
              "sky": ("enabled", "zenith", "horizon", "ground", "intensity", "sun_tint", "ambient"),
              "fog": ("enabled", "density", "height", "offset", "noise", "scale", "wind", "color", "fill", "sky")}
LIGHT_TYPES = ("point", "spot", "rect")
FOG_SHAPES = ("box", "sphere")


def _spec(kind, key):
    k = SCHEMA.get(kind)
    if not k:
        return None
    for f in k[2]:
        if f[0] == key:
            return f
    return None


def field_decimals(kind, key):
    f = _spec(kind, key)
    if f:
        return f[1]
    k = SCHEMA.get(kind)
    return k[1] if k else 4


def known_field(kind, key):
    return _spec(kind, key) is not None or bool(SCHEMA.get(kind, (False,))[0])


def volatile(kind, name, key):
    f = _spec(kind, key)
    return bool(f and f[3]) and not (kind == "glow" and name.startswith("@"))


def format_number(v, decimals):
    s = "%.*f" % (4 if decimals < 0 else decimals, v)
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


def parse_numbers(text):
    if not text:
        return None
    out = []
    for part in text.split(","):
        try:
            v = float(part)
        except ValueError:
            return None
        if not math.isfinite(v) or part.strip() != part or part == "":
            return None
        out.append(v)
    return out


def canonical_value(kind, key, value):
    d = field_decimals(kind, key)
    if d < 0:
        return value
    nums = parse_numbers(value)
    if nums is None:
        return value
    return ",".join(format_number(v, d) for v in nums)


def _rank(kind, key):
    k = SCHEMA.get(kind)
    if k:
        for i, f in enumerate(k[2]):
            if f[0] == key:
                return i
    return 1000


# The fields a fog preset (pc/assets/environment/fog_presets.json) can give a volume.
PRESET_FIELDS = ("shape", "size", "density", "edge", "color", "fill", "noise", "sim", "sim_strength",
                 "sim_recover", "sim_footprint", "sim_trail", "sim_wind", "detail", "sim_swirl", "shadow", "disturb_player",
                 "disturb_actors", "disturb_attacks", "disturb_spells")


def canonicalize(kind, fields):
    """fields: list of (key, value) -> canonical list (order, values, defaults dropped; a fog volume
    with a preset keeps its preset fields even at their defaults: they differ from the preset's)."""
    out = {}
    fields = list(fields)
    preset = kind == "fog" and any(key == "preset" for key, _ in fields)
    for key, value in fields:
        v = canonical_value(kind, key, value)
        f = _spec(kind, key)
        if f and f[2] is not None and v == canonical_value(kind, key, f[2]) \
                and not (preset and key in PRESET_FIELDS):
            out.pop(key, None)
            continue
        out[key] = v
    return sorted(out.items(), key=lambda kv: (_rank(kind, kv[0]), kv[0]))


class Item:
    def __init__(self, kind, name, fields=()):
        self.kind, self.name = kind, name
        self.fields = canonicalize(kind, fields)

    @property
    def id(self):
        return f"{self.kind}:{self.name}"

    def get(self, key):
        return dict(self.fields).get(key)

    def __eq__(self, other):
        return (self.kind, self.name, self.fields) == (other.kind, other.name, other.fields)

    def __repr__(self):
        return format_entry(Entry("define", self.id, item=self))


class Entry:
    def __init__(self, verb, id, item=None, set=(), unset=(), line=0):
        self.verb, self.id, self.item, self.set, self.unset, self.line = verb, id, item, list(set), list(unset), line


class ParseError(Exception):
    pass


_ID = re.compile(r"^[a-z_]+:[^\s#\"=]+$")
_KIND = re.compile(r"^[a-z_]+$")


def tokenize(line):
    """Words of a line: (key, value or None, unset). A # outside quotes ends it."""
    out, i, n = [], 0, len(line)
    while i < n:
        while i < n and line[i] in " \t\r\n":
            i += 1
        if i >= n or line[i] == "#":
            break
        unset = line[i] == "!"
        if unset:
            i += 1
        start = i
        while i < n and line[i] not in "= \t\r\n":
            i += 1
        key, value = line[start:i], None
        if i < n and line[i] == "=":
            i += 1
            if i < n and line[i] == '"':
                i += 1
                value, closed = "", False
                while i < n:
                    c = line[i]
                    i += 1
                    if c == "\\" and i < n:
                        value += line[i]
                        i += 1
                        continue
                    if c == '"':
                        closed = True
                        break
                    value += c
                if not closed:
                    raise ParseError("unterminated quote")
            else:
                start = i
                while i < n and line[i] not in " \t\r\n":
                    i += 1
                value = line[start:i]
        if not key:
            raise ParseError("empty key")
        out.append((key, value, unset))
    return out


def parse_line(line):
    """-> ("header", version) | ("entry", Entry) | None (blank/comment)."""
    toks = tokenize(line)
    if not toks:
        return None
    verb, value, unset = toks[0]
    if value is not None or unset:
        raise ParseError(f"expected a kind or verb, not '{verb}='")
    if verb == "rood-lighting":
        if len(toks) < 2 or toks[1][1] is not None:
            raise ParseError("header: rood-lighting <version>")
        try:
            return ("header", int(toks[1][0]))
        except ValueError:
            raise ParseError("header: rood-lighting <version>")
    if len(toks) < 2 or toks[1][1] is not None or toks[1][2]:
        raise ParseError(f"'{verb}' needs an id")
    if verb in ("override", "delete"):
        target = toks[1][0]
        if not _ID.match(target):
            raise ParseError(f"bad id '{target}' (kind:name)")
        kind = target.split(":", 1)[0]
        e = Entry("override" if verb == "override" else "delete", target)
        for key, value, unset in toks[2:]:
            if verb == "delete":
                raise ParseError("delete takes only an id")
            if unset:
                if value is not None:
                    raise ParseError(f"!{key} takes no value")
                e.unset.append(key)
            elif value is None:
                raise ParseError(f"expected key=value, got '{key}'")
            else:
                e.set.append((key, canonical_value(kind, key, value)))
        return ("entry", e)
    if not _KIND.match(verb):
        raise ParseError(f"bad kind '{verb}'")
    name = toks[1][0]
    if not _ID.match(f"{verb}:{name}"):
        raise ParseError(f"bad id '{verb}:{name}'")
    fields = []
    for key, value, unset in toks[2:]:
        if unset or value is None:
            raise ParseError(f"expected key=value, got '{'!' if unset else ''}{key}'")
        fields.append((key, value))
    item = Item(verb, name, fields)
    return ("entry", Entry("define", item.id, item=item))


def is_v2(text):
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        return s.startswith("rood-lighting")
    return False


class File:
    def __init__(self, path=None):
        self.path, self.exists, self.version, self.entries, self.errors = path, False, 0, [], []


def parse_text(text, path=None):
    f = File(path)
    f.exists = True
    if not is_v2(text):
        f.version = 0
        if text.strip():
            f.errors.append((0, "old format (pc/lights): packs need a 'rood-lighting 2' header (the game migrates "
                                "user files when it saves them)"))
        return f
    for number, line in enumerate(text.splitlines(), 1):
        try:
            r = parse_line(line)
        except ParseError as error:
            f.errors.append((number, str(error)))
            continue
        if r is None:
            continue
        if r[0] == "header":
            f.version = r[1]
        else:
            r[1].line = number
            f.entries.append(r[1])
    if f.version > FORMAT_VERSION:
        f.errors.append((0, f"format version {f.version} is newer than this tool reads ({FORMAT_VERSION})"))
    return f


def read_file(path):
    try:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
    except FileNotFoundError:
        return File(path)
    return parse_text(text, path)


def _quote(v):
    if v and not v.startswith(("\"", "!")) and not any(c <= " " or c in "\"#\\" for c in v):
        return v
    return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'


def format_entry(e):
    if e.verb == "define":
        return " ".join([e.item.kind, e.item.name] + [f"{k}={_quote(v)}" for k, v in e.item.fields])
    if e.verb == "override":
        return " ".join(["override", e.id] + [f"{k}={_quote(v)}" for k, v in e.set] + [f"!{k}" for k in e.unset])
    return f"delete {e.id}"


def format_file(entries, comment=""):
    """The whole file, entries sorted by id (deterministic)."""
    lines = [f"rood-lighting {FORMAT_VERSION}"]
    if comment:
        lines.append(f"# {comment}")
    lines += [format_entry(e) for e in sorted(entries, key=lambda e: e.id)]
    return "\n".join(lines) + "\n"


def generated_id(kind, *source):
    """A deterministic id for a generated item: kind, then 'gen.' and a
    short hash of what it was generated from (fixture type, key, ...), so
    regenerating keeps ids (and the overrides that name them)."""
    digest = hashlib.sha1("\x1f".join(str(s) for s in source).encode()).hexdigest()[:10]
    tag = re.sub(r"[^a-z0-9_]+", "_", str(source[0]).lower()).strip("_")[:24] if source else "item"
    return f"{kind}:gen.{tag}.{digest}"


def apply_entry(state, e):
    """Apply to state (id -> Item); returns a problem string or None."""
    if e.verb == "define":
        state[e.id] = e.item
        return None
    if e.id not in state:
        return f"{e.verb} of {e.id}: no layer below defines it"
    if e.verb == "delete":
        del state[e.id]
        return None
    it = state[e.id]
    fields = dict(it.fields)
    fields.update(e.set)
    for k in e.unset:
        fields.pop(k, None)
    state[e.id] = Item(it.kind, it.name, fields.items())
    return None


def _stable(it):
    return [(k, "" if volatile(it.kind, it.name, k) else v) for k, v in it.fields]


def same_item(a, b):
    return a.kind == b.kind and _stable(a) == _stable(b)


def diff_one(below, desired, id):
    b, d = below.get(id), desired.get(id)
    if d is None:
        return Entry("delete", id) if b is not None else None
    if b is None:
        return Entry("define", id, item=d)
    if same_item(b, d):
        return None
    if b.kind not in ("light", "actor", "env", "fog", "post"):
        return Entry("define", id, item=d)
    old = dict(b.fields)
    sets = [(k, v) for k, v in d.fields if not (k in old and (old[k] == v or volatile(d.kind, d.name, k)))]
    unset = [k for k, _ in b.fields if d.get(k) is None]
    return Entry("override", id, set=sets, unset=unset) if sets or unset else None


def diff(below, desired):
    out = []
    for id in sorted(set(below) | set(desired)):
        e = diff_one(below, desired, id)
        if e:
            out.append(e)
    return out


def room_file_name(zone, room):
    return f"zone{zone:02d}_room{room:02d}.txt"


class Layer:
    def __init__(self, kind, pack, path):
        self.kind, self.pack, self.path = kind, pack, path
        self.file = read_file(path)

    @property
    def label(self):
        return "user" if self.kind == "user" else f"{self.pack}/{self.kind}"

    @property
    def badge(self):
        return {"generated": "G", "rooms": "P", "user": "U"}[self.kind]


class Room:
    pass


def load_room(zone, room, root="lighting", packs=("default",), user=None):
    """Every layer of a room, merged (as the game does)."""
    r = Room()
    r.zone, r.room, r.layers = zone, room, []
    name = room_file_name(zone, room)
    for pack in packs:
        for kind in ("generated", "rooms"):
            r.layers.append(Layer(kind, pack, os.path.join(root, pack, kind, name)))
    if user:
        r.layers.append(Layer("user", "", os.path.join(user, name)))
    r.problems = []
    for i, layer in enumerate(r.layers):
        for line, message in layer.file.errors:
            r.problems.append((i, line, "", message))
    upto = len(r.layers) - (1 if user else 0)
    r.base = merge(r.layers[:upto], r.problems)
    r.effective = merge(r.layers, None) if user else dict(r.base)
    if user:
        top = []
        merge(r.layers, top)
        r.problems += [p for p in top if p[0] == len(r.layers) - 1 and p[2]]
    return r


def merge(layers, problems=None):
    state = {}
    for i, layer in enumerate(layers):
        for e in layer.file.entries:
            problem = apply_entry(state, e)
            if problem and problems is not None:
                problems.append((i, e.line, e.id, problem))
    return state
