#!/usr/bin/env python3
"""Small ELF fixtures: reloadable engines must not own snapshot sections."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import engine_boundary
import engine_lib


class SavedStateGuardTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def object(self, name, storage="", kind="data", prefix=None):
        source = self.root / (name + ".c")
        obj = source.with_suffix(".o")
        source.write_text(f"{storage} volatile int value{' = 7' if kind == 'data' else ''};\n"
                          "int get_value(void) { return value; }\n")
        subprocess.run(["gcc", "-fPIC", "-c", str(source), "-o", str(obj)], check=True)
        if prefix:
            subprocess.run(["objcopy", "--rename-section", f".{kind}={prefix}{kind}", str(obj)], check=True)
        return obj

    def link(self, obj, output):
        subprocess.run(["gcc", "-shared", str(obj), "-o", str(output)], check=True)

    def test_objects_and_linked_libraries(self):
        for storage in ("", "static"):
            for kind in ("data", "bss"):
                for prefix in ("rood_state_", "rood_ovl_test_"):
                    with self.subTest(storage=storage, kind=kind, prefix=prefix):
                        obj = self.object("fixture", storage, kind, prefix)
                        with self.assertRaisesRegex(RuntimeError, prefix + kind):
                            engine_boundary.check_saved_sections(obj)
                        # Initial boundary generation rejects before writing any files.
                        with self.assertRaisesRegex(RuntimeError, "host-owned"):
                            engine_boundary.generate([], [obj], lambda _: "", {}, [], [], self.root)
                        self.assertFalse((self.root / "engine_boundary.json").exists())
                        staged = self.root / "engine.so.new"
                        self.link(obj, staged)
                        # The linked guard must not rely on local symbols surviving.
                        subprocess.run(["strip", "--strip-all", str(staged)], check=True)
                        library = self.root / "engine.so"
                        library.write_bytes(b"previous library")
                        with self.assertRaisesRegex(RuntimeError, prefix + kind):
                            engine_boundary.publish_library(staged, library)
                        self.assertEqual(library.read_bytes(), b"previous library")
                        self.assertFalse(staged.exists())

    def test_empty_and_ordinary_sections_allowed(self):
        obj = self.object("ordinary")
        engine_boundary.check_saved_sections(obj)
        empty = self.root / "empty.s"
        empty.write_text('.section rood_state_data,"aw",@progbits\n'
                         '.section rood_ovl_test_bss,"aw",@nobits\n')
        subprocess.run(["gcc", "-c", str(empty), "-o", str(self.root / "empty.o")], check=True)
        engine_boundary.check_saved_sections(self.root / "empty.o")
        staged, library = self.root / "good.new", self.root / "good.so"
        self.link(obj, staged)
        engine_boundary.publish_library(staged, library)
        self.assertTrue(library.exists())
        self.assertFalse(staged.exists())

    def test_incremental_rejection_preserves_published_library(self):
        bad = self.object("bad", "static", "bss", "rood_state_")
        good = self.object("good")
        for object_rejection in (True, False):
            with self.subTest(object_rejection=object_rejection):
                unit = bad if object_rejection else good
                unit.with_suffix(".d").write_text(f"unit: {unit.with_suffix('.c')}\n")
                library = self.root / "librood_engine.so"
                library.write_bytes(b"previous library")
                os.utime(library, ns=(1, 1))  # force relink, no recompilation
                host = self.root / "rood_host"
                host.touch()
                manifest = {"host_depfiles": [], "host_units": [], "shaders": [],
                            "units": [{"object": unit.name, "command": []}],
                            # Simulate saved bytes introduced by a linked archive.
                            "link": ["gcc", "-shared", str(bad), "-o", str(library)]}
                (self.root / "engine_library.json").write_text(json.dumps(manifest))
                (self.root / "engine_boundary.json").write_text(json.dumps(
                    {"functions": [], "data": {}, "exports": []}))
                with patch.multiple(engine_lib, ROOT=self.root, OUT=self.root,
                                    BUILD_SCRIPTS=[], SHARED=[]), \
                     patch.object(sys, "argv", ["engine_lib.py", "--quiet"]), \
                     contextlib.redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(engine_lib.main(), 2)
                self.assertIn("rood_state_bss", errors.getvalue())
                self.assertEqual(library.read_bytes(), b"previous library")
                self.assertFalse(library.with_name(library.name + ".new").exists())


if __name__ == "__main__":
    unittest.main()
