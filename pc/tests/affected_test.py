#!/usr/bin/env python3
"""The --changed selector (affected.py, affected.toml) and the quick lane:
tracked files only, tool folders ignored, no full-suite fallback, long suites
only when touched, zig_diff only on the specs a change affects (every spec
when shared infrastructure changed), and recent merges selecting tens of
suites, not all."""
import contextlib
import io
import pathlib
import statistics
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

import affected
import run
import zig_diff_changed

sys.path.insert(0, str(run.ROOT / "pc/tools"))
import zig_diff_select  # noqa: E402


def all_suites(renderer="vulkan"):
    args = run.default_args()
    args.renderer = renderer
    args.save = pathlib.Path(__file__)  # an existing file: the suites that need a card are listed too
    return run.renderer_suites(args, "egl")


class ChangedFilesTest(unittest.TestCase):
    """changed_files on a scratch repository."""

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], capture_output=True, text=True,
                              check=True).stdout

    def commit(self, files, message):
        for name, text in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            self.git("add", name)
        self.git("commit", "-q", "-m", message)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "test@example.com")
        self.git("config", "user.name", "test")
        self.commit({"README.md": "a\n", "pc/src/a.c": "1\n", "pc/src/b.c": "1\n"}, "base")
        self.patch = patch.object(affected, "ROOT", self.root)
        self.patch.start()

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def test_untracked_and_tool_folders_never_count(self):
        self.git("branch", "base")
        self.commit({"pc/src/a.c": "2\n"}, "change a")
        (self.root / ".kameosa/worktrees/x").mkdir(parents=True)
        (self.root / ".kameosa/worktrees/x/file.c").write_text("x")
        (self.root / ".claude").mkdir()
        (self.root / ".claude/settings.json").write_text("{}")
        (self.root / "pc/src/new_untracked.c").write_text("x")
        (self.root / "pc/src/b.c").write_text("2\n")  # a tracked edit, uncommitted
        self.assertEqual(affected.changed_files("base"), ["pc/src/a.c", "pc/src/b.c"])

    def test_tool_folders_dropped_even_when_tracked(self):
        self.git("branch", "base")
        self.commit({".kameosa/state.json": "{}", ".claude/x.md": "x", "pc/src/a.c": "3\n"}, "tracked tools")
        self.assertEqual(affected.changed_files("base"), ["pc/src/a.c"])

    def test_branch_behind_base_counts_only_its_own_commits(self):
        self.git("checkout", "-q", "-b", "work")
        self.commit({"pc/src/a.c": "work\n"}, "work")
        self.git("checkout", "-q", "main")
        self.commit({"pc/src/b.c": "main moved\n"}, "main moves on")
        self.git("checkout", "-q", "work")
        self.assertEqual(affected.changed_files("main"), ["pc/src/a.c"])

    def test_published_snapshot_uses_the_tree_diff(self):
        # A snapshot: main's tree as one commit with no shared recent history.
        for i in range(3):
            self.commit({f"src/f{i}.c": "x\n"}, f"history {i}")
        tree = self.git("rev-parse", "HEAD^{tree}").strip()
        snapshot = self.git("commit-tree", tree, "-m", "snapshot").strip()
        self.git("branch", "public", snapshot)
        self.commit({"pc/src/a.c": "after publish\n"}, "unpublished")
        self.assertEqual(affected.changed_files("public"), ["pc/src/a.c"])
        self.assertEqual(affected.base_revision("public"), snapshot)

    def test_base_revision_of_a_branch_is_the_merge_base(self):
        fork = self.git("rev-parse", "HEAD").strip()
        self.git("checkout", "-q", "-b", "work")
        self.commit({"pc/src/a.c": "work\n"}, "work")
        self.git("checkout", "-q", "main")
        self.commit({"pc/src/b.c": "main moved\n"}, "main moves on")
        self.git("checkout", "-q", "work")
        self.assertEqual(affected.base_revision("main"), fork)


class SelectTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.suites = all_suites()
        cls.names = {s.name for s in cls.suites}
        cls.table = affected.load_table()
        cls.broad = set(affected.expander(list(cls.names), cls.table)(["@broad"]))

    def select(self, *files):
        return affected.select(list(files), self.suites, self.table)

    def test_every_name_in_the_table_exists(self):
        names = {s.name for r in ("vulkan", "gl", "null") for s in all_suites(r)}
        table = self.table
        entries = [("core", table["core"]["suites"]), ("baseline", table["baseline"]["suites"]),
                   ("default", table["default"]["suites"]), ("shared", table["shared"]["suites"])]
        entries += [(f"set {k}", v) for k, v in table["sets"].items()]
        entries += [(f"quick {k}", v) for k, v in table["quick"].items() if k != "gl_paths"]
        entries += [(f"long {e['paths'][:1]}", e["suites"]) for e in table["long"]]
        entries += [(f"rule {r['paths'][:2]}", r["suites"]) for r in table["rule"]]
        expand = affected.expander(sorted(names), table)
        for where, patterns in entries:
            for pattern in patterns:
                with self.subTest(where=where, pattern=pattern):
                    self.assertTrue(expand([pattern]), f"{pattern} names no suite")

    def test_no_file_selects_every_suite(self):
        for path in ["pc/src/brand_new_file.c", "pc/tools/rood_rpc.py", "src/SLUS_010.40/main.c",
                     "pc/src/vk_hw.c", "pc/tests/run.py", "Makefile", "weird/place.txt"]:
            with self.subTest(path=path):
                sel = self.select(path)
                self.assertLess(len(sel.reasons), 40, sorted(sel.reasons))

    def test_unknown_file_selects_the_default_set_with_a_warning(self):
        sel = self.select("weird/place.txt")
        self.assertEqual(sel.unknown, ["weird/place.txt"])
        self.assertEqual(set(sel.reasons), set(affected.expander(list(self.names), self.table)(["@default"])))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            affected.report(sel, len(self.suites))
        self.assertIn("warning: no rule", out.getvalue())
        self.assertNotIn("the full suite", out.getvalue())

    def test_core_files_select_the_broad_set(self):
        for path in ["pc/tools/rood_rpc.py", "pc/src/boot_runtime.c", "src/SLUS_010.40/main.c"]:
            with self.subTest(path=path):
                sel = self.select(path)
                self.assertTrue(self.broad <= set(sel.reasons))
                self.assertLess(len(sel.reasons), len(self.broad) + 10)

    def test_overlay_config_maps_to_its_overlay(self):
        sel = self.select("config/BATTLE/BATTLE.PRG/Makefile")
        self.assertIn("damage_numbers_smoke", sel.reasons)
        self.assertNotIn("ui_map_smoke", sel.reasons)
        self.assertFalse(sel.core)
        sel = self.select("config/MENU/MENU0.PRG/splat.yaml")
        self.assertIn("save_menu_smoke", sel.reasons)
        self.assertNotIn("soak", sel.reasons)

    def test_docs_and_selector_changes(self):
        self.assertFalse(self.select("pc/docs/Testing.md", "README.md").reasons)
        self.assertEqual(set(self.select("pc/tests/affected.toml").reasons), {"affected_test", "verify_args_test"})

    def test_shared_test_module_selects_the_broad_set(self):
        sel = self.select("pc/tests/checkpoints.py")
        self.assertLess(len(sel.reasons), len(self.broad) + 10)
        sel = self.select("pc/tests/golden_frames.py")  # imported by many; it is also a suite's script
        self.assertIn("golden_frames", sel.reasons)
        self.assertLess(len(sel.reasons), len(self.broad) + 10)


class ZigDiffSelectTest(unittest.TestCase):
    """pc/tools/zig_diff_select.py on the checked-in tables (old texts made up)."""
    @classmethod
    def setUpClass(cls):
        root = run.ROOT
        cls.specs_text = (root / zig_diff_select.SPECS_TOML).read_text()
        cls.own_text = (root / zig_diff_select.OWNERSHIP).read_text()
        cls.total = len(tomllib.loads(cls.specs_text)["function"])

    def select(self, *files, old=None):
        return zig_diff_select.select(list(files), old)

    def test_port_and_spec_files(self):
        sel = self.select("game/battle/score.zig")
        self.assertEqual(sel.functions, ["_updateEnemyKills"])
        self.assertEqual(sel.summary(), f"zig_diff: 1 changed spec (of {self.total})")
        sel = self.select("pc/tests/zig_diff/findAnimation.zig", "game/battle/actor_transform.zig")
        self.assertEqual(sel.functions, ["func_800AD494", "func_800B07DC"])
        self.assertEqual(sel.why["func_800B07DC"], ["game/battle/actor_transform.zig"])
        self.assertRegex(sel.summary(), r"^zig_diff: 2 changed specs \(of \d+\)$")

    def test_d2_port_files_and_oracle_units(self):
        # a D2 port lives in game/battle/pc/ and its unit is the pc/src oracle
        sel = self.select("game/battle/pc/tick_state_timers.zig")
        self.assertEqual(sel.functions, ["func_800E4358"])
        sel = self.select("pc/src/battle_motion.c")
        self.assertEqual(sel.functions, ["func_800B65E8", "func_800B65F4"])
        self.assertIsNone(sel.fallback)

    def test_files_no_run_reads(self):
        sel = self.select("game/battle/root.zig", "game/core/root.zig", "game/common/route_test.zig",
                          "pc/tests/zig_diff/bakeoff.toml", "pc/tests/zig_diff_coverage_test.py")
        self.assertEqual(sel.functions, [])
        self.assertIsNone(sel.fallback)

    def test_infrastructure_falls_back_to_every_spec(self):
        for path in [*zig_diff_select.INFRA, "game/common/common.zig", "game/common/route.zig"]:
            with self.subTest(path=path):
                sel = self.select(path, "game/battle/score.zig")
                self.assertIsNone(sel.functions)
                self.assertIn(path, sel.fallback)
                self.assertTrue(sel.summary().startswith(f"zig_diff: every spec ({self.total}): {path}"))

    def test_specs_toml_entries_diffed(self):
        changed = self.specs_text.replace('[function._updateEnemyKills]\nspec = "updateEnemyKills.zig"\n'
                                          'iterations = 40000', '[function._updateEnemyKills]\n'
                                          'spec = "updateEnemyKills.zig"\niterations = 30000')
        self.assertNotEqual(changed, self.specs_text)
        old = {zig_diff_select.SPECS_TOML: changed}.get
        self.assertEqual(self.select(zig_diff_select.SPECS_TOML, old=old).functions, ["_updateEnemyKills"])
        same = {zig_diff_select.SPECS_TOML: "# a comment only\n" + self.specs_text}.get
        self.assertEqual(self.select(zig_diff_select.SPECS_TOML, old=same).functions, [])
        new_file = self.select(zig_diff_select.SPECS_TOML, old=lambda path: None)  # added since the base
        self.assertEqual(len(new_file.functions), self.total)
        self.assertIsNotNone(self.select(zig_diff_select.SPECS_TOML).fallback)  # no base: every spec

    def test_ownership_routes_diffed(self):
        own = zig_diff_select.OWNERSHIP
        moved = self.own_text.replace('zig = "score.updateEnemyKills"', 'zig = "score_old.updateEnemyKills"')
        self.assertNotEqual(moved, self.own_text)
        self.assertEqual(self.select(own, old={own: moved}.get).functions, ["_updateEnemyKills"])
        # a unit's views are every function of the unit's
        views = self.own_text.replace('globals = ["vs_main_scoredata"]', 'globals = []', 1)
        self.assertNotEqual(views, self.own_text)
        sel = self.select(own, old={own: views}.get)
        routes = zig_diff_select._routes(self.own_text)
        unit = routes["_updateEnemyKills"][1]
        self.assertIn("_updateEnemyKills", sel.functions)
        self.assertTrue(all(routes[f][1] == unit for f in sel.functions))
        self.assertGreater(len(sel.functions), 1)
        self.assertEqual(self.select(own, old={own: self.own_text}.get).functions, [])

    def test_pipeline_records_and_units(self):
        sel = self.select("pc/docs/porting/battle/pipeline/146C/_addMinDelta.json")
        self.assertEqual(sel.functions, ["_addMinDelta"])
        sel = self.select("src/BATTLE/BATTLE.PRG/44F14.c")
        self.assertIn("func_800B07DC", sel.functions)
        self.assertNotIn("_updateEnemyKills", sel.functions)

    def test_runner_shards(self):
        cmds = zig_diff_changed.commands(["a", "b", "c", "d"], 3, pathlib.Path("build"), ctest=False)
        self.assertEqual([c[-5] for _, c in cmds], ["0/3", "1/3", "2/3"])
        self.assertTrue(all(c[2:6] == ["a", "b", "c", "d"] for _, c in cmds))
        self.assertEqual(len(zig_diff_changed.commands(["a"], 3, pathlib.Path("build"), ctest=False)), 1)
        self.assertEqual(zig_diff_changed.commands([], 3, pathlib.Path("build"), ctest=False), [])


class QuickLaneTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = affected.load_table()
        cls.lanes = {r: all_suites(r) for r in ("vulkan", "gl", "null")}

    def lane(self, renderer, *files):
        suites = self.lanes[renderer]
        sel = affected.select(list(files), suites, self.table)
        return affected.quick_lane(sel, suites, renderer, self.table, run.NULL_SAFE)

    def test_docs_only_runs_nothing(self):
        for renderer in self.lanes:
            self.assertEqual(self.lane(renderer, "pc/docs/Testing.md").names, [])

    def test_fixed_extras(self):
        vk = self.lane("vulkan", "pc/src/ui/ui_map.cpp")
        self.assertIn("ctest", vk.names)
        self.assertNotIn("ctest_long", vk.names)
        null = self.lane("null", "pc/src/ui/ui_map.cpp")
        self.assertEqual(null.names, ["null_backend_smoke"])
        gl = self.lane("gl", "pc/src/ui/ui_map.cpp")
        self.assertEqual(set(gl.names), set(self.table["quick"]["gl_sanity"]))

    def test_gl_files_run_their_suites_on_gl(self):
        gl = self.lane("gl", "pc/src/gpu_hw.c")
        self.assertIn("ui_text_smoke", gl.names)
        self.assertNotIn("ctest", gl.names)  # renderer-independent: the Vulkan lane ran it

    def test_long_suites_only_when_touched(self):
        vk = self.lane("vulkan", "src/BATTLE/BATTLE.PRG/146C.c")
        for name in ("soak", "skip_charging_smoke", "charge_skip_soak"):
            self.assertNotIn(name, vk.names)
            self.assertIn(name, vk.skipped_long)
        self.assertIn("damage_numbers_smoke", vk.names)
        self.assertIn("skip_charging_smoke", self.lane("vulkan", "pc/src/charge_skip.c").names)
        self.assertIn("soak", self.lane("vulkan", "pc/tests/soak.py").names)
        self.assertIn("ctest_long", self.lane("vulkan", "game/battle/score.zig").names)

    def test_zig_change_runs_only_its_specs(self):
        vk = self.lane("vulkan", "game/battle/score.zig")
        self.assertEqual(vk.zig.functions, ["_updateEnemyKills"])
        self.assertRegex(affected.lane_line(vk, 100), r"zig_diff: 1 changed spec \(of \d+\)")
        self.assertIsNone(self.lane("vulkan", "src/MENU/MENU0.PRG/A.c").zig)  # no ctest_long, no zig_diff
        suite = run.zig_diff_changed(vk.zig.functions)
        self.assertEqual(suite.name, "zig_diff_changed")
        self.assertIn("_updateEnemyKills", suite.command)

    def test_zig_infrastructure_runs_every_spec(self):
        for path in ("pc/tools/zig_diff.py", "game/common/common.zig", "pc/tests/zig_diff/main.zig"):
            with self.subTest(path=path):
                vk = self.lane("vulkan", path, "game/battle/score.zig")
                self.assertIn("ctest_long", vk.names)
                self.assertIsNone(vk.zig.functions)
                self.assertIn(path, vk.zig.summary())
                self.assertIn("every spec", affected.lane_line(vk, 100))

    def test_null_safe_suites_run_on_null_not_vulkan(self):
        files = ("game/battle/score.zig",)
        self.assertIn("trial_corpus_kill", self.lane("null", *files).names)
        self.assertNotIn("trial_corpus_kill", self.lane("vulkan", *files).names)

    def test_recent_merges_select_tens_not_all(self):
        """The last merges on main (none in a published snapshot's history):
        each quick Vulkan lane well under the full suite, most of them tens.
        A merge that changes the test runner or the selector itself selects
        most suites by design, so it is left out."""
        log = subprocess.run(["git", "-C", str(run.ROOT), "log", "--merges", "--first-parent", "-25",
                              "--format=%H", "HEAD"], capture_output=True, text=True)
        merges = log.stdout.split()
        if log.returncode or len(merges) < 5:
            self.skipTest("no merge history here")
        runner = {"pc/tests/run.py", "pc/tests/affected.py", "pc/tests/affected.toml"}
        total = len(self.lanes["vulkan"])
        counts = []
        for merge in merges:
            files = subprocess.run(["git", "-C", str(run.ROOT), "diff", "--name-only", "--no-renames",
                                    f"{merge}^1", merge], capture_output=True, text=True, check=True).stdout.split()
            if runner & set(files):
                continue
            counts.append(len(self.lane("vulkan", *files).names))
        self.assertLess(max(counts), total * 0.7, counts)
        self.assertLess(statistics.median(counts), 80, counts)


if __name__ == "__main__":
    unittest.main()
