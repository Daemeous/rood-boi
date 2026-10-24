#!/usr/bin/env python3
"""Disc-free regression tests for generated rect-light emissive matching.

    python3 pc/tests/window_lights_test.py
"""
import collections
import copy
import hashlib
import pathlib
import sys
import unittest

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import generate_lighting as gl


class WindowLightsTest(unittest.TestCase):
    def setUp(self):
        self.vram = np.zeros((512, 1024), dtype=np.uint16)
        # Two palette uses of the same index image; red/green versus blue.
        self.vram[0, 0] = 0x2121
        self.vram[0, 1] = 0x2121
        self.vram[1, 1:3] = [31, 31 << 5]
        self.vram[1, 17:19] = [31 << 10, 31 << 10]
        self.p = dict(ikey="texture", off=100, flags=0, tpage=0, clut=64,
                      uv=np.array([[0, 0], [1, 0], [1, 1], [0, 1]]),
                      V=np.array([[0., 0, 0], [16, 0, 0], [16, 16, 0], [0, 16, 0]]),
                      normal=np.array([0., 0, 1]), area=256)
        self.r = dict(zone=1, room=2, polys=[self.p])
        self.e = dict(id="E999", match=[dict(key="texture", size=[2, 2])], rect_light={})

    def lights(self):
        return gl.window_lights([self.e], self.r, self.vram, collections.Counter())

    def color(self):
        lights = self.lights()
        self.assertEqual(len(lights), 1)
        return dict(lights[0].fields)["color"]

    def test_palette_reuse_and_matching_alternative(self):
        palette = hashlib.md5(self.vram[1, :16].astype("<u2").tobytes()).hexdigest()[:10]
        self.e["match"][0]["clut"] = palette
        other = copy.deepcopy(self.p)
        other.update(off=200, clut=65, V=self.p["V"] + [100, 0, 0])
        self.r["polys"].append(other)
        self.assertEqual(len(self.lights()), 1)
        self.assertEqual(self.color(), "1,1,0")
        self.e["match"].insert(0, dict(key="texture", clut="wrong"))
        self.assertEqual(len(self.lights()), 1)
        self.e["match"] = [dict(key="texture")]
        self.assertEqual(len(self.lights()), 2)

    def test_scopes_exclude_room_surfaces_not_models(self):
        other = copy.deepcopy(self.p)
        other.update(off=200, V=self.p["V"] + [16, 0, 0])
        self.r["polys"].append(other)  # touches: must not enlarge the allowed pane
        baseline = None
        for scope, count in [({}, 1), ({"rooms": [[9, 9]]}, 0),
                             ({"models": ["12345678"]}, 0),
                             ({"surfaces": [{"room": [9, 9], "polygons": [100]}]}, 0),
                             ({"surfaces": [{"room": [1, 2], "polygons": [999]}]}, 0),
                             ({"rooms": [[1, 2]], "surfaces": [{"room": [1, 2], "polygons": [100]}],
                               "models": ["12345678"]}, 1)]:
            with self.subTest(scope=scope):
                self.e["scope"] = scope
                lights = self.lights()
                self.assertEqual(len(lights), count)
                if scope.get("surfaces") and count:
                    baseline = lights
        self.r["polys"] = [self.p]
        self.e.pop("scope")
        self.assertEqual(baseline, self.lights())

    def test_shapes_offsets_union_and_filters(self):
        self.e["match"][0]["offset"] = [10, 20]
        self.e["mask"] = {"rects": [[10, 20, 11, 22]]}
        self.assertEqual(self.color(), "1,0,0")  # exclusive upper edge
        self.e["mask"] = {"polygons": [[[11, 20], [12, 20], [12, 22], [11, 22]]]}
        self.assertEqual(self.color(), "0,1,0")
        self.e["mask"]["rects"] = [[10, 20, 11, 22]]
        self.assertEqual(self.color(), "1,1,0")  # shapes union
        for filter_ in [{"indices": [1]}, {"color_min": [31, 0, 0]},
                        {"color_max": [31, 0, 31]}, {"luma": [0.29, 0.30]}]:
            with self.subTest(filter=filter_):
                self.e["mask"] = dict(rects=[[10, 20, 12, 22]], **filter_)
                self.assertEqual(self.color(), "1,0,0")
        self.e["mask"] = {"rects": [[11, 20, 12, 22]], "indices": [1]}
        self.assertEqual(self.lights(), [])  # filters intersect shapes, no white fallback

    def test_empty_masks_and_transparency(self):
        for mask in [{"indices": []}, {"rects": [[9, 9, 10, 10]]}, {"luma": [0.9, 1]}]:
            self.e["mask"] = mask
            self.assertEqual(self.lights(), [])
        self.e["mask"] = {}
        self.vram[1, 1:3] = 0
        self.assertEqual(self.lights(), [])

    def test_mask_does_not_crop_pane_geometry(self):
        other = copy.deepcopy(self.p)
        other.update(off=200, clut=65, V=self.p["V"] + [16, 0, 0])
        self.r["polys"].append(other)
        self.e["mask"] = {"color_max": [31, 31, 0]}  # blue half contributes no color
        fields = dict(self.lights()[0].fields)
        self.assertEqual(fields["size"], "32,16")
        self.assertEqual(fields["color"], "1,1,0")

    def test_direct_color_ignores_index_filter(self):
        self.p["tpage"] = 256
        self.vram[:2, :2] = 31
        self.e["mask"] = {"indices": []}
        self.assertEqual(self.color(), "1,0,0")

    def test_painted_mask_fails_explicitly(self):
        self.e["mask"] = {"map": "emissive"}
        with self.assertRaisesRegex(ValueError, "painted masks"):
            self.lights()


if __name__ == "__main__":
    unittest.main()
