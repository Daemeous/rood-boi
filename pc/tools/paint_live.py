#!/usr/bin/env python3
"""The live bridge of paint_roundtrip.py (`live <pkg>`): a WebSocket server on localhost that takes the strokes the Aseprite extension
(pc/tools/aseprite/) sends and pushes the map into the running game's material layer in memory (RPC material map_set: no file per
stroke; the saved .aseprite file, imported by the watch that runs beside it, is what persists).

The layers are the channel table's (pc/tools/aseprite/channels.json: Height, Metal, Roughness...) and Material (preset swatches:
pc/tools/material_presets.py); the bridge keeps the last cel of each per material and combines them as the import does (pr.layered_map:
a channel layer's pixel that differs from the game's value wins, else the Material layer's preset, else the game's value). The greeting
tells the extension the layers, so it carries no list of its own.

A message from the extension is one binary frame: u32 big-endian header length, the header (JSON: material, layer, x, y, w, h,
canvas [W, H]: the cel of that channel layer, its offset on the canvas), then the cel's w x h RGBA bytes. A text frame is "hello" (the reply is the greeting) or the cursor: {"locate": {"material": the sprite's title, "x", "y"}}, the texel
the painter's cursor is over (the extension's hover), which the bridge turns into the texture's content key and the place in it (the page's
slices: a model's page is boxes of textures) and gives the running game's studio (RPC studio locate): the texel's footprint is marked on
the summoned model. The other way, the bridge watches the studio's pick (RPC studio picked: a hover or a click on the model in the
studio's window) and sends the extension {"select": {"material", "x", "y", "scroll"}}: the texel to select (and, for a click, to scroll
the view to). The reply is a text frame (JSON:
ok, game: the game is reachable, note; the greeting adds the presets: name, colour, about (their values as text), and the layers). Importing a layer is exactly the import's: transparent keeps the game's value (over_seed),
a texel's gray is its map value, the catalog entry of the painted object is made once."""
import argparse
import base64
import json
import pathlib
import socketserver
import struct
import sys
import threading
import time

import numpy as np

import miniws
import paint_channels as pch
import paint_roundtrip as pr
import rood_rpc
import aseprite_io as ase
import material_presets as mp



class Bridge:
    def __init__(self, folder, rpc=None, layer=None, catalog=None):
        self.folder = pathlib.Path(folder)
        self.seed = json.loads((self.folder / "seed.json").read_text())
        self.labels = {pr.safe(label): (label, rec) for label, rec in self.seed["labels"].items()}
        self.rpc = rpc or rood_rpc.DEFAULT_SOCKET
        self.ns = argparse.Namespace(dir=str(self.folder), layer=layer, catalog=catalog, normal_format="auto", keep_seed=False)
        self.game, self.sent, self.entries, self.lock = None, {}, set(), threading.Lock()
        self.pick_serial = None  # the studio's pick last passed on (None: the first look only learns it)
        self.layers = {}  # label -> {channel or "material": the canvas the extension sent last}
        self.preset_stamp, self.presets = None, []
        self.reload_presets()

    def connect(self):
        if self.game is None:
            try:
                self.game = rood_rpc.Game(self.rpc)
                self.sent.clear()  # (a game that is new has none of what was sent)
            except (OSError, rood_rpc.RpcError):
                self.game = None
        return self.game is not None

    def push(self, key, obj, channel, array):
        """The map into the game; False when the game is not there."""
        if not self.connect():
            return False
        h, w = array.shape
        try:
            self.game.call("material", action="map_set", key=key, object=obj, channel=channel, w=w, h=h,
                           data=base64.b64encode(np.ascontiguousarray(array).tobytes()).decode())
        except (rood_rpc.RpcError, OSError, ValueError):
            self.game = None  # (the game went away, or restarted: connect again on the next stroke)
            return False
        return True

    def clear(self, key, obj, channel):
        """The game's own map of the channel again (the map set in memory taken away); False when the game is not there."""
        if not self.connect():
            return False
        try:
            self.game.call("material", action="map_clear", key=key, object=obj, channel=channel)
        except (rood_rpc.RpcError, OSError, ValueError):
            self.game = None
            return False
        return True

    def reload_presets(self):
        """The preset files again when one changed; True then."""
        stamp = mp.stamp()
        if stamp == self.preset_stamp:
            return False
        self.preset_stamp = stamp
        self.presets = mp.load(warn=lambda text: print(text, flush=True))
        return True

    def preset_list(self):
        """The presets for the extension's window: [{name, colour, about}] (about: 'metal 1, roughness 0.12', the channels the preset sets)."""
        return [{"name": p["name"], "colour": "#%02x%02x%02x" % p["rgb"], "about": mp.about(p)} for p in self.presets]

    def greeting(self, note):
        return {"ok": True, "game": self.game is not None, "note": note, "presets": self.preset_list(), **pch.greeting()}

    def layers_of(self, label, rec):
        """The layer canvases the bridge knows of a material: what the extension sent, else the saved file's (first use)."""
        if label not in self.layers:
            path = pr.ase_path(self.folder, pr.safe(label))
            self.layers[label] = {name: canvas for name, canvas in pr.read_ase_channels(path).items()} if path.exists() else {}
        return self.layers[label]

    def recompute(self, label, rec, channels):
        """The maps of `channels` from the layers the bridge holds (layer, Material, the game's own: pr.layered_map), pushed where they
        changed: the number pushed, and whether the game took them."""
        h, w = rec["h"], rec["w"]
        layers = self.layers_of(label, rec)
        material = layers.get("material")
        expanded = mp.expand(material, self.presets) if material is not None else {}
        pushed, alive = 0, True
        for channel in channels:
            was = pr.painted_seed(self.folder, rec, channel, h, w)
            was = was if was is not None else pr.neutral_map(channel, h, w)
            now = pr.layered_map(layers.get(channel), was, expanded.get(channel))
            last = self.sent.get((label, channel))
            if last is not None and np.array_equal(last, now):
                continue
            if last is None and np.array_equal(now, was):
                continue  # (nothing painted yet: the game's own map stands, as an unchanged import writes none; a height layer left at the seed must not flatten the estimate)
            targets = []
            if rec.get("slices"):  # a model's page: each box is a texture of the game
                for sl in rec["slices"]:
                    u0, v0, u1, v1 = sl["box"]
                    part = np.ascontiguousarray(now[v0:v1 + 1, u0:u1 + 1])
                    if last is None or not np.array_equal(part, last[v0:v1 + 1, u0:u1 + 1]):
                        targets.append(({**rec, "key": sl["key"], "w": u1 - u0 + 1, "h": v1 - v0 + 1, "maps": sl.get("maps")}, part))
            else:
                targets.append((rec, now))
            fresh = [(label, r, [channel]) for r, _ in targets if (label, r["key"]) not in self.entries]
            if fresh:  # (the catalog entry that makes the object use its maps, once)
                with pr._catalog_lock:
                    pr.ensure_entries(self.ns, self.seed, fresh)
                self.entries.update((label, r["key"]) for _, r, _ in fresh)
            # (a layer back at the game's values: where the game has no map for it, no map is put back either, so its own estimate or entry
            # value shows again; a package from before the record has them all)
            back = np.array_equal(now, was)
            ok = all([self.clear(r["key"], r["object"], channel) if back and r.get("maps") is not None and channel not in r["maps"]
                      else self.push(r["key"], r["object"], channel, part) for r, part in targets]) if targets else self.connect()
            alive = alive and ok
            if ok:
                self.sent[(label, channel)] = now
            pushed += 1
        return pushed, alive

    def apply(self, header, pixels):
        """One layer's cel (a channel's, or Material): returns the reply dict."""
        material = self.labels.get(header.get("material", ""))
        layer = str(header.get("layer", "")).strip().lower()
        channel = pch.layer_channel(layer)
        if not material or not channel:
            return {"ok": False, "game": self.game is not None, "note": f"{header.get('material')!r} / {header.get('layer')!r} is not a material or layer of this package"}
        label, rec = material
        h, w = rec["h"], rec["w"]
        if tuple(header.get("canvas", ())) != (w, h):
            return {"ok": False, "game": self.game is not None, "note": f"the canvas is {header.get('canvas')}, the texture {w} x {h}: do not resize it"}
        cw, ch = int(header["w"]), int(header["h"])
        if len(pixels) != cw * ch * 4:
            return {"ok": False, "game": self.game is not None, "note": "the pixels are not w x h RGBA"}
        canvas = ase.to_canvas(w, h, int(header["x"]), int(header["y"]), np.frombuffer(pixels, np.uint8).reshape(ch, cw, 4))
        with self.lock:
            reloaded = self.reload_presets()
            self.layers_of(label, rec)[channel] = canvas
            # (a Metal or Roughness layer changes its own map, the Material layer both; a changed preset file, both of every material held)
            pushed, alive = self.recompute(label, rec, pch.PRESET_CHANNELS if channel == "material" or reloaded else (channel,))
            if reloaded:
                for other_label, other_rec in self.labels.values():
                    if other_label != label and other_label in self.layers:
                        n, ok = self.recompute(other_label, other_rec, pch.PRESET_CHANNELS)
                        pushed, alive = pushed + n, alive and ok
        if not pushed:
            return {"ok": True, "game": self.game is not None, "note": "unchanged"}
        return {"ok": True, "game": alive, "note": "" if alive else "the game is not running (the saved file still imports)"}

    # ------------------------------------------------------------------------------------------------- the texel locator

    def texture_of(self, label, x, y):
        """A page pixel as (texture content key, x, y in the texture), or None: a model's page is boxes of textures (the seed's slices),
        a room's texture is the whole sprite."""
        material = self.labels.get(label)
        if not material:
            return None
        rec = material[1]
        if rec.get("slices"):
            inside = [sl for sl in rec["slices"] if sl["box"][0] <= x <= sl["box"][2] and sl["box"][1] <= y <= sl["box"][3]]
            if not inside:
                return None
            sl = min(inside, key=lambda sl: (sl["box"][2] - sl["box"][0]) * (sl["box"][3] - sl["box"][1]))
            return sl["key"], x - sl["box"][0], y - sl["box"][1]
        return (rec["key"], x, y) if 0 <= x < rec["w"] and 0 <= y < rec["h"] else None

    def pixel_of(self, key, lx, ly, model=""):
        """A texel of a texture as (the sprite's title, x, y on its canvas); of several pages with the texture the model's own is preferred."""
        found = []
        for title, (label, rec) in self.labels.items():
            for sl in rec.get("slices") or [{"key": rec["key"], "box": [0, 0, rec["w"] - 1, rec["h"] - 1]}]:
                if sl["key"] == key and lx <= sl["box"][2] - sl["box"][0] and ly <= sl["box"][3] - sl["box"][1]:
                    found.append((0 if model and model in (rec.get("model"), rec.get("model_legacy")) else 1, title,
                                  sl["box"][0] + lx, sl["box"][1] + ly))
        return min(found)[1:] if found else None

    def locate(self, header):
        """The painter's cursor: the texel marked on the model in the game's studio. The reply is the layer messages' (ok, game, note)."""
        title, x, y = str(header.get("material", "")), int(header.get("x", -1)), int(header.get("y", -1))
        at = self.texture_of(title, x, y)
        if not at:
            return {"ok": True, "game": self.game is not None, "locate": True, "note": ""}  # (a texel outside any texture, or a sprite that is not ours)
        with self.lock:
            if not self.connect():
                return {"ok": True, "game": False, "locate": True, "note": ""}
            try:
                got = self.game.call("studio", action="locate", key=at[0], x=at[1], y=at[2])
            except rood_rpc.RpcError as e:
                return {"ok": True, "game": True, "locate": True, "note": f"the studio: {e}"}
            except (OSError, ValueError):
                self.game = None
                return {"ok": True, "game": False, "locate": True, "note": ""}
        found = got.get("locate", {})
        return {"ok": True, "game": True, "locate": True, "triangles": found.get("triangles", 0),
                "note": "" if found.get("triangles") else "that texel is not on the model shown"}

    def picked(self):
        """The studio's newest pick as the extension's select message (a dict), when it is one not passed on yet; else None."""
        with self.lock:
            if not self.connect():
                return None
            try:
                pick = self.game.call("studio", action="picked").get("pick") or {}
            except rood_rpc.RpcError:
                return None  # (no studio in this game)
            except (OSError, ValueError):
                self.game = None
                return None
        serial = pick.get("serial", 0)
        first = self.pick_serial is None
        if serial == self.pick_serial or serial is None:
            return None
        self.pick_serial = serial
        if first or not pick.get("hit") or not pick.get("key"):
            return None
        where = self.pixel_of(pick["key"], int(pick["lx"]), int(pick["ly"]), pick.get("model", ""))
        if not where:
            return None
        return {"select": {"material": where[0], "x": where[1], "y": where[2], "scroll": bool(pick.get("scroll"))}}

    def message(self, ws, kind, data):
        started = time.monotonic()
        try:
            if kind == miniws.TEXT:
                text = data.decode("utf-8", "replace").strip()
                if text.startswith("{"):
                    header = json.loads(text)
                    if isinstance(header.get("locate"), dict):
                        reply = self.locate(header["locate"])
                        reply["ms"] = round((time.monotonic() - started) * 1000, 1)
                        return reply
                self.connect()
                self.reload_presets()
                return self.greeting("hello")
            (n,) = struct.unpack(">I", data[:4])
            reply = self.apply(json.loads(data[4:4 + n].decode()), data[4 + n:])
        except (ValueError, KeyError, struct.error, TypeError, AttributeError) as e:
            reply = {"ok": False, "game": self.game is not None, "note": f"bad message: {e}"}
        reply["ms"] = round((time.monotonic() - started) * 1000, 1)
        return reply


PICK_POLL = 0.1  # seconds between looks at the studio's pick


def serve(bridge, port, log=lambda text: print(text, flush=True)):
    """The server: a thread per connection; returns the (started) server (`shutdown()` ends it)."""
    class Handler(socketserver.BaseRequestHandler):
        def handle(self):
            ws = miniws.WebSocket(self.request)
            try:
                ws.accept()
            except (miniws.Closed, OSError):
                return
            log(f"live: Aseprite connected")
            sending, alive = threading.Lock(), threading.Event()

            def send(text):
                with sending:
                    ws.send(text)

            def watch_picks():  # (the studio's pick, to the painter: select that texel)
                bridge.pick_serial = None
                while not alive.wait(PICK_POLL):
                    try:
                        message = bridge.picked()
                        if message:
                            send(json.dumps(message))
                    except OSError:
                        return
            bridge.connect()
            send(json.dumps(bridge.greeting("connected")))
            threading.Thread(target=watch_picks, daemon=True).start()
            while True:
                got = ws.recv()
                if got is None:
                    break
                try:
                    send(json.dumps(bridge.message(ws, *got)))
                except OSError:
                    break
            alive.set()
            log("live: Aseprite disconnected")

    class Server(socketserver.ThreadingTCPServer):
        allow_reuse_address = True
        daemon_threads = True
    server = Server(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def live(args):
    bridge = Bridge(args.dir, rpc=args.rpc, layer=args.layer, catalog=args.catalog)
    server = serve(bridge, args.port)
    print(f"live: listening on ws://127.0.0.1:{args.port} for the Aseprite extension; "
          f"game {'reachable' if bridge.connect() else 'not running (strokes are kept for when it is; saved files still import)'}", flush=True)
    stop = threading.Event()
    if not args.no_watch:
        threading.Thread(target=pr.watch_package, args=(args, stop), daemon=True).start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("live: stopped")
    finally:
        stop.set()
        server.shutdown()
    return 0
