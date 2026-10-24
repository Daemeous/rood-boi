#!/usr/bin/env python3
"""Synthetic window mapping tests: no disc or extracted images required."""
import collections
import json
import tempfile
import pathlib
import sys
import unittest
from unittest.mock import patch

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import generate_lighting as gl
import lighting_format as lf


class WindowPattern(unittest.TestCase):
    def pane(self, flipped=False):
        return dict(V=np.array([[0., 0, 0], [16., 0, 0], [0., 16, 0], [16., 16, 0]]),
                    uv=np.array([[23, 40], [8, 40], [23, 55], [8, 55]]) if flipped else
                       np.array([[8, 40], [23, 40], [8, 55], [23, 55]]),
                    quad=True, ikey="0123456789", normal=np.array([0., 0, -1]), area=256,
                    flags=0, off=1234, tpage=0, clut=0)

    def test_flipped_uv(self):
        p = self.pane(True)
        text = gl.window_pattern_faces([p], [0], np.array([8., 8, 0]),
                                       np.array([1., 0, 0]), np.array([0., 1, 0]), [16, 16])
        triangles = [f.split(",") for f in text.split(";")]
        self.assertEqual(len(triangles), 2)
        self.assertEqual(triangles[0][0], p["ikey"] + "#1234")
        points = np.array(triangles[0][1:], float).reshape(3, 4)
        np.testing.assert_allclose(points, [[0, 0, 15.5/16, .5/16], [1, 0, .5/16, .5/16],
                                            [0, 1, 15.5/16, 15.5/16]])
        self.assertNotIn("mask", text)

    def test_generator_defaults_and_uniform_switch(self):
        p = self.pane()
        entry = dict(id="E999", match=[dict(key=p["ikey"])], rect_light={})
        room = dict(zone=1, room=0, polys=[p])
        def texel(_vram, u, v, _page, _clut):
            return np.full(u.shape, 32767), np.ones(u.shape, int)
        with patch.object(gl.fl, "texel", texel):
            items = gl.window_lights([entry], room, None, collections.Counter())
            self.assertEqual(len(items), 1)
            fields = dict(items[0].fields)
            self.assertEqual(fields["pattern"], "1")
            self.assertEqual(fields.get("pattern_grid", "16"), "16")
            self.assertIn(p["ikey"], fields["pattern_faces"])
            entry["rect_light"]["pattern"] = 0
            off = gl.window_lights([entry], room, None, collections.Counter())[0]
            self.assertEqual(dict(off.fields).get("pattern", "0"), "0")
            # Disabling does not discard the identity / mapping needed to re-enable.
            self.assertEqual(dict(off.fields)["pattern_faces"], fields["pattern_faces"])
            self.assertEqual(off.name, items[0].name)
            text = gl.room_text(1, 0, items)
            self.assertIn("pattern_faces=", text)
            self.assertIn("pattern=1", text)

    def test_scope_and_palette(self):
        p = self.pane()
        vram = np.zeros((512, 1024), dtype=np.uint16)
        vram[0, :16] = np.arange(16)
        palette = gl.window_clut_key(vram, p)
        e = dict(match=[dict(key=p["ikey"], clut=palette)],
                 scope=dict(surfaces=[dict(room=[28, 14], polygons=[1234])]))
        r = dict(zone=28, room=14)
        self.assertTrue(gl.window_polygon_matches(e, p, r, vram))
        p["off"] = 5678
        self.assertFalse(gl.window_polygon_matches(e, p, r, vram))
        p["off"] = 1234
        p["clut"] = 1  # same index-key texture, different palette: never a source
        self.assertFalse(gl.window_polygon_matches(e, p, r, vram))
        p["clut"] = 0
        self.assertFalse(gl.window_polygon_matches(e, p, dict(zone=28, room=8), vram))
        e["scope"] = dict(models=["12345678"])
        self.assertFalse(gl.window_polygon_matches(e, p, r, vram))

    def test_single_texel_centres(self):
        p = self.pane()
        p["uv"][:] = [23, 40]
        text = gl.window_pattern_faces([p], [0], np.array([8., 8, 0]),
                                       np.array([1., 0, 0]), np.array([0., 1, 0]), [16, 16])
        points = np.array(text.split(";")[0].split(",")[1:], float).reshape(3, 4)
        np.testing.assert_allclose(points[:, 2:], .5)

    def test_explicit_user_layer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            shipped, user = root / "shipped.json", root / "user.json"
            shipped.write_text(json.dumps({"entries": [{"id": "E001", "rect_light": {"pattern": 1}}]}))
            user.write_text(json.dumps({"entries": [{"id": "E001", "rect_light": {"pattern": 0}}]}))
            self.assertEqual(gl.load_window_entries(shipped)[0]["rect_light"]["pattern"], 1)
            self.assertEqual(gl.load_window_entries(shipped, user)[0]["rect_light"]["pattern"], 0)

    def test_schema(self):
        keys = {field[0] for field in lf.SCHEMA["light"][2]}
        self.assertTrue({"pattern", "pattern_grid", "pattern_faces"} <= keys)


if __name__ == "__main__":
    unittest.main()
