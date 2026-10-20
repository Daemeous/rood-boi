#!/usr/bin/env python3
"""CPU-only protocol fixtures. These tests do not establish game coverage."""
import pathlib
import os
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from diff import corpus, differ, scenarios


class Game:
    def __init__(self):
        self.events = []
        self.current = "original"
        self.tick_result = {"error": 0, "ticks": 600, "recorded": 600, "first_tick": -1}
        self.shadow = {"calls": 4, "count": 0, "errors": 0,
                       "coverage": [{"function": "f", "calls": 4, "nested": 0}], "mismatches": []}

    def digest(self, action, **kw):
        self.events.append(("digest", action, kw))
        return dict(self.tick_result)

    def replay(self, path):
        self.events.append(("replay", path))

    def rewind(self):
        self.events.append(("rewind",))

    def mark(self):
        self.events.append(("mark",))

    def owner(self, subsystem, owner):
        self.current = owner
        self.events.append(("owner", owner))

    def record(self, path):
        if path:
            pathlib.Path(path).write_bytes(b"fixture input")
        self.events.append(("record", path))

    def run(self, ticks):
        self.events.append(("run", ticks, self.current))

    def call(self, method, **kw):
        if method == "snapshot":
            if kw["action"] == "hash":
                return {"ram": "fixture", "vram": "fixture"}
            pathlib.Path(kw["path"]).write_bytes(b"fixture snapshot")
        if method == "ui_menu":
            return {"visible": False}
        return dict(self.shadow)


class Tests(unittest.TestCase):
    def test_clock_mask_is_qualified_and_fails_closed(self):
        completed = SimpleNamespace(stdout='ORACLE_CLOCK=[4096, 16]\n')
        with patch.object(corpus.subprocess, "run", return_value=completed) as run:
            self.assertEqual(corpus.clock_mask("target"), [[4096, 16]])
            self.assertIn("'boot_runtime.c'::last_vsync", run.call_args.args[0][-1])
        with patch.object(corpus.subprocess, "run", return_value=SimpleNamespace(stdout='')):
            with self.assertRaises(RuntimeError):
                corpus.clock_mask("target")

    def compare(self, game, modes=differ.MODES, diagnose=lambda *args: {"field": "actor.hp"}):
        mapper = SimpleNamespace(names=lambda addresses: {a: "actor.hp" for a in addresses})
        return differ.compare(game, "recording", 600, "battle_action", 2, modes,
                              mapper, pathlib.Path("."), ["f", "uncalled"], diagnose)

    def test_each_owner_rewinds_same_game_and_control_first(self):
        game = Game()
        results = self.compare(game)
        self.assertTrue(all(r["ok"] for r in results.values()))
        self.assertEqual(sum(e == ("rewind",) for e in game.events), 4)
        self.assertEqual([e[2] for e in game.events if e[0] == "run"],
                         ["original", "shadow", "owned", "owned"])
        self.assertIn(("digest", "compare", {"alternate": 2}), game.events)
        self.assertEqual(results["call"]["coverage_summary"]["uncalled"], ["uncalled"])

    def test_control_difference_stops_owned_runs(self):
        game = Game()
        game.tick_result["first_tick"] = 7
        results = self.compare(game)
        self.assertEqual(list(results), ["control"])
        self.assertFalse(results["control"]["ok"])

    def test_incomplete_digest_is_failure(self):
        for field, value in (("error", 1), ("ticks", 599), ("recorded", 599)):
            game = Game()
            game.tick_result[field] = value
            self.assertFalse(self.compare(game)["control"]["ok"])

    def test_shadow_errors_and_zero_calls_fail(self):
        for field, value in (("calls", 0), ("errors", 1), ("count", 1), ("truncated", True)):
            game = Game()
            game.shadow[field] = value
            self.assertFalse(self.compare(game)["call"]["ok"])

    def test_shadow_field_annotation(self):
        game = Game()
        game.shadow.update(count=1, mismatches=[{"first": 123, "range": "memory"}])
        self.assertEqual(self.compare(game)["call"]["mismatches"][0]["field"], "actor.hp")

    def test_owned_mismatch_diagnostic(self):
        game = Game()
        old_run = game.run
        def run(ticks):
            old_run(ticks)
            if game.current == "owned":
                game.tick_result["first_tick"] = 42
        game.run = run
        result = self.compare(game, ["tick"])["tick"]
        self.assertFalse(result["ok"])
        self.assertEqual(result["first_difference"]["field"], "actor.hp")

    def test_first_tick_snapshot_mapping_preserves_alternation(self):
        game = Game()
        mapper = SimpleNamespace(names=lambda addresses: {a: "actor.hp" for a in addresses})
        states = [{"memory": {100: b"abc"}}, {"memory": {100: b"axc"}}]
        with tempfile.TemporaryDirectory() as directory:
            with patch("snapshot_diff.read", side_effect=states):
                result = differ.first_difference(game, pathlib.Path(directory), "recording",
                                                 42, "battle_action", 2, "alternate", mapper)
        self.assertEqual(result["address"], 101)
        self.assertEqual(result["field"], "actor.hp")
        self.assertEqual([e[1] for e in game.events if e[0] == "run"], [43, 43])
        self.assertIn(("digest", "compare", {"alternate": 2}), game.events)

    def test_missing_complex_scenarios_are_not_aliases(self):
        selected = scenarios.select()
        self.assertGreater(len(selected), 20)
        for scenario in selected:
            if scenario.name != "enemy_cast_wait":
                with self.assertRaises(NotImplementedError):
                    scenario.setup(Game())

    def test_cache_rejects_escape_and_symlink(self):
        with self.assertRaises(ValueError):
            corpus.cache_path(corpus.CACHE / "../../outside")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "cache").mkdir()
            (root / "cache/link").symlink_to(root, target_is_directory=True)
            with patch.object(corpus, "CACHE", root / "cache"):
                with self.assertRaises(ValueError):
                    corpus.cache_path(root / "cache/link/out")

    def test_profile_generation_refuses_shared_checkpoint_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            work = root / "work"; work.mkdir()
            card = root / "card"; card.write_bytes(b"synthetic fixture")
            def launch(*args, **kwargs):
                self.assertEqual(os.environ["ROOD_TEST_SNAPSHOTS"], "0")
                raise RuntimeError("stop before launching fixture")
            table = SimpleNamespace(subsystems=[SimpleNamespace(name="battle_action")])
            with patch.dict(os.environ, {"ROOD_TEST_SNAPSHOTS": "1"}), \
                 patch("ownership.load", return_value=table), patch("checkpoints.launch_at", side_effect=launch):
                with self.assertRaisesRegex(RuntimeError, "stop before"):
                    differ.run_one(None, SimpleNamespace(subsystem="battle_action", card=card), work)
                self.assertEqual(os.environ["ROOD_TEST_SNAPSHOTS"], "1")

    def test_current_five_field_enemy_cast_witness(self):
        game = SimpleNamespace(call=lambda method: {"ended": "2:54:1:0:0 0:7:1:0:0"})
        result = scenarios.SCENARIOS["enemy_cast_wait"].witness(game)
        self.assertTrue(result["ok"])
        self.assertEqual(result["enemy_casts"], [["2", "54", "1", "0", "0"]])

    def test_profile_has_classic_feature_gates(self):
        values = dict(line.split() for line in corpus.PROFILE.splitlines())
        for key in ("qol_skip_charging", "qol_fast_travel", "ui_menus", "widescreen", "camera_stick"):
            self.assertEqual(values[key], "0")

    def test_generator_pins_state_input_profile_and_marks_once(self):
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            game = Game()
            corpus.environment(work, "battle_action")
            scenario = SimpleNamespace(name="fixture", ticks=600, setup=lambda g: None,
                                       play=lambda g: g.run(600), reset_witness=lambda g: None,
                                       witness=lambda g: {"ok": True})
            recording, ticks = corpus.generate(game, scenario, work, "battle_action")
            self.assertEqual(ticks, 600)
            self.assertTrue(recording.exists())
            self.assertIn(corpus.sha256(recording), (work / "manifest.json").read_text())
            self.assertEqual(game.events.count(("mark",)), 1)

    def test_incomplete_selection_exits_nonzero_without_game(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(corpus, "CACHE", pathlib.Path(directory)):
                self.assertEqual(differ.main(["--scenario", "chain", "--out", directory]), 1)

    def test_room_transition_has_bounded_failure(self):
        game = Game()
        game.call = lambda *a, **k: {"phase": 1}
        with self.assertRaises(TimeoutError):
            scenarios.SCENARIOS["enemy_cast_wait"].setup(game)
        self.assertEqual(len(game.events), 80)


if __name__ == "__main__":
    unittest.main()
