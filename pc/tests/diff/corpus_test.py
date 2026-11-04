#!/usr/bin/env python3
"""CPU-only protocol fixtures. These tests do not establish game coverage."""
import contextlib
import io
import pathlib
import os
import re
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
        self.tick_result = {"error": 0, "ticks": 600, "recorded": 600, "first_tick": -1,
                            "alternate_calls": [{"function": "f", "owned": 2, "original": 2},
                                                {"function": "uncalled", "owned": 0, "original": 0}]}
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
        return differ.compare(game, "recording", 600, "battle_44F14", 2, modes,
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

    def test_alternate_needs_both_sides_of_every_called_function(self):
        for calls in ([{"function": "f", "owned": 3, "original": 0}],
                      [{"function": "f", "owned": 0, "original": 1}],
                      [{"function": "f", "owned": 0, "original": 0}], None):
            game = Game()
            game.tick_result["alternate_calls"] = calls
            results = self.compare(game)
            self.assertFalse(results["alternate"]["ok"], calls)
            self.assertTrue(results["tick"]["ok"])

    def test_alternate_one_side_waiver(self):
        calls = [{"function": "f", "owned": 1, "original": 0}, {"function": "g", "owned": 2, "original": 2}]
        self.assertFalse(differ.alternate_check(calls)["ok"])
        check = differ.alternate_check(calls, {"f": "one call per recipe"})
        self.assertTrue(check["ok"])
        self.assertEqual(check["waived"], {"f": "one call per recipe"})
        self.assertFalse(differ.alternate_check(calls, {"g": "unused"})["ok"])

    def test_root_call_gate_needs_every_function_or_a_waiver(self):
        reports = {"a": {"modes": {"call": {"coverage_summary": {"called": ["f"]}}}},
                   "b": {"modes": {"call": {"coverage_summary": {"called": ["g"]}}}}}
        with tempfile.TemporaryDirectory() as directory:
            waivers = pathlib.Path(directory) / "waivers.toml"
            waivers.write_text('[sub]\nh = "no recipe yet"\n')
            self.assertTrue(differ.root_call_gate(reports, "sub", ["f", "g", "h"], waivers)["ok"])
            gate = differ.root_call_gate(reports, "sub", ["f", "g", "h", "k"], waivers)
            self.assertFalse(gate["ok"])
            self.assertEqual(gate["missing"], ["k"])
            self.assertEqual(gate["waived"], {"h": "no recipe yet"})
            self.assertFalse(differ.root_call_gate(reports, "other", ["f", "h"], waivers)["ok"])

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
                                                 42, "battle_44F14", 2, "alternate", mapper)
        self.assertEqual(result["address"], 101)
        self.assertEqual(result["field"], "actor.hp")
        self.assertEqual([e[1] for e in game.events if e[0] == "run"], [43, 43])
        self.assertIn(("digest", "compare", {"alternate": 2}), game.events)

    def test_missing_complex_scenarios_are_not_aliases(self):
        selected = scenarios.select()
        self.assertGreater(len(selected), 20)
        for scenario in selected:
            if scenario.name not in scenarios.IMPLEMENTED:
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
            table = SimpleNamespace(subsystems=[SimpleNamespace(name="battle_44F14")])
            with patch.dict(os.environ, {"ROOD_TEST_SNAPSHOTS": "1"}), \
                 patch("ownership.load", return_value=table), patch("checkpoints.launch_at", side_effect=launch):
                with self.assertRaisesRegex(RuntimeError, "stop before"):
                    differ.run_one(None, SimpleNamespace(subsystem="battle_44F14", card=card), work)
                self.assertEqual(os.environ["ROOD_TEST_SNAPSHOTS"], "1")

    def test_current_five_field_enemy_cast_witness(self):
        game = SimpleNamespace(call=lambda method: {"ended": "2:54:1:0:0 0:7:1:0:0"})
        result = scenarios.SCENARIOS["enemy_cast_wait"].witness(game)
        self.assertTrue(result["ok"])
        self.assertEqual(result["enemy_casts"], [["2", "54", "1", "0", "0"]])

    def test_name_window_witness_is_exact(self):
        scenario = scenarios.SCENARIOS["action_name_window"]
        good = " ".join(scenario.EXPECTED)
        game = SimpleNamespace(call=lambda method: {"name": {"names": good}, "ended": ""})
        self.assertTrue(scenario.witness(game)["ok"])
        for names in (" ".join(scenario.EXPECTED[:2]), good.replace("f800fb04", "f800fb05", 1),
                      "54:f800fb04 " + " ".join(scenario.EXPECTED[1:])):
            game = SimpleNamespace(call=lambda method, n=names: {"name": {"names": n}, "ended": ""})
            self.assertFalse(scenario.witness(game)["ok"], names)

    def test_kill_witness_is_exact(self):
        import struct
        scenario = scenarios.SCENARIOS["enemy_kill"]
        memory = bytearray(0x110)

        def state(kills, streak, score, flags=(18, 14)):
            data = bytearray(0x110)
            struct.pack_into("<6H", data, 4, *kills)
            for t in flags:
                data[0x9C + 4 * (t // 32) + (t % 32) // 8] |= 1 << (t % 8)
            data[0x10C:0x10E] = bytes(streak)
            struct.pack_into("<I", data, 0x104, score)
            return data
        game = SimpleNamespace(read=lambda address, length: bytes(memory[address - 0x1000:][:length]))
        with patch("rood_rpc.symbols", return_value={"vs_main_scoredata": 0x1000}):
            object.__setattr__(scenario, "_subtypes", [18, 14])
            memory[:] = state((4, 16, 0, 0, 0, 0), (1, 99), 0, flags=())
            scenario.reset_witness(game)
            memory[:] = state((4, 17, 0, 1, 0, 0), (3, 0), 8197)
            self.assertTrue(scenario.witness(game)["ok"])
            for wrong in (state((4, 18, 0, 1, 0, 0), (3, 0), 8197), state((4, 17, 0, 1, 0, 0), (3, 1), 8197),
                          state((4, 17, 0, 1, 0, 0), (3, 0), 8274), state((4, 17, 0, 1, 0, 0), (3, 0), 8197, (18,))):
                memory[:] = wrong
                self.assertFalse(scenario.witness(game)["ok"])

    def test_profile_has_classic_feature_gates(self):
        values = dict(line.split() for line in corpus.PROFILE.splitlines())
        for key in ("qol_skip_charging", "qol_fast_travel", "ui_menus", "widescreen", "camera_stick"):
            self.assertEqual(values[key], "0")

    def test_features_profile_turns_on_state_changing_hooks_only(self):
        values = dict(line.split() for line in corpus.FEATURES.splitlines())
        self.assertEqual(values["qol_skip_charging"], "1")
        for key in ("qol_confirm_cross", "qol_field_cross_attack", "ui_menus", "camera_stick"):
            self.assertEqual(values[key], "0")
        with tempfile.TemporaryDirectory() as directory:
            corpus.environment(pathlib.Path(directory), "battle_44F14", "features")
            self.assertIn("qol_skip_charging 1", (pathlib.Path(directory) / "settings.cfg").read_text())

    def test_generator_pins_state_input_profile_and_marks_once(self):
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            game = Game()
            corpus.environment(work, "battle_44F14")
            scenario = SimpleNamespace(name="fixture", ticks=600, setup=lambda g: None,
                                       play=lambda g: g.run(600), reset_witness=lambda g: None,
                                       witness=lambda g: {"ok": True})
            recording, ticks = corpus.generate(game, scenario, work, "battle_44F14")
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


SRC = pathlib.Path(__file__).resolve().parents[2] / "src"


def settings_keys():
    """Every key the settings reader knows: the entries table, the strcmp keys of
    project_settings.cpp and the readers it hands a key to (read as text, no build)."""
    keys = set()
    text = (SRC / "project_settings.cpp").read_text()
    keys.update(re.findall(r'\b(?:I|F|FN|IN|B)\("([a-z0-9_]+)"', text))
    keys.update(re.findall(r'strcmp\(key, "([a-z0-9_]+)"\)', text))
    for name in ("ui_text.c", "button_prompts.c"):
        keys.update(re.findall(r'strcmp\(key, "([a-z0-9_]+)"\)', (SRC / name).read_text()))
    return keys


def parse_profile(text):
    values = {}
    for line in text.splitlines():
        key, value = line.split(None, 1)
        assert key not in values, f"{key} pinned twice"
        values[key] = value.strip()
    return values


class InertProfile(unittest.TestCase):
    """The post-flip profile (FLIP.md section 3): CPU-only, no game files. It checks the
    profile's data and the runner switch; whether the game honours each line is the
    corpus run's."""

    def test_pins_every_classic_key_and_adds_its_own(self):
        classic, inert = parse_profile(corpus.PROFILE), parse_profile(corpus.PROFILES["inert"])
        self.assertEqual({k: v for k, v in inert.items() if k in classic}, classic)
        self.assertEqual({k: v for k, v in inert.items() if k not in classic},
                         {k: str(v) for k, v in corpus.INERT_PINS.items()})
        self.assertTrue(set(corpus.OFF) <= set(inert))
        for key in corpus.OFF:
            self.assertEqual(inert[key], "0", key)

    def test_every_key_exists_in_the_settings_reader(self):
        known = settings_keys()
        for profile in corpus.PROFILES:
            for key in parse_profile(corpus.PROFILES[profile]):
                self.assertIn(key, known, f"{key} ({profile}) is not a settings key")

    def test_every_hud_classic_key_is_pinned(self):
        text = (SRC / "project_settings.cpp").read_text()
        declared = set(re.findall(r'\bI\("(hud_classic_[a-z0-9_]+)"', text))
        pinned = {k for k in parse_profile(corpus.PROFILES["inert"]) if k.startswith("hud_classic_")}
        self.assertEqual(pinned, declared)
        self.assertEqual({v for k, v in parse_profile(corpus.PROFILES["inert"]).items() if k in pinned}, {"0"})

    def test_words_are_the_readers_own(self):
        inert = parse_profile(corpus.PROFILES["inert"])
        prompts = (SRC / "button_prompts.c").read_text()
        self.assertRegex(prompts, r'pc_button_prompts_names\[PC_PROMPTS_MODES\] = \{[^}]*"original"')
        self.assertEqual(inert["button_prompts"], "original")
        self.assertEqual(inert["ui_text"], "original")
        self.assertEqual(inert["ui_bubbles"], "original")
        self.assertEqual(inert["camera_stick_menus"], "none")
        self.assertIn('"none"', (SRC / "camera_stick.c").read_text())

    def test_runner_switch_writes_the_profile_and_its_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            env = corpus.environment(work, "battle_44F14", "inert")
            self.assertEqual(env["ROOD_SETTINGS"], str(work / "settings.cfg"))
            self.assertEqual((work / "settings.cfg").read_text(), corpus.PROFILES["inert"])
            self.assertEqual(env["ROOD_OPACITY_2D"], "0")
            self.assertEqual(env["ROOD_OWNER"], "battle_44F14=original")
            self.assertEqual(env["ROOD_ORACLE"], "1")
            self.assertNotIn("ROOD_OPACITY_2D", corpus.environment(work, "battle_44F14", "classic"))

    def test_differ_offers_the_profile(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(differ.main(["--list", "--profile", "inert"]), 0)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            differ.main(["--list", "--profile", "no-such-profile"])


if __name__ == "__main__":
    unittest.main()
