#!/usr/bin/env python3
"""CPU-only regression tests for the fight's per-vsync measurement."""
import unittest

from glow_stability_smoke import fight_samples


class FakeGame:
    def __init__(self, *, step=1, returned_status=True, hit_over=False):
        self.vsync = 12000
        self.step = step
        self.returned_status = returned_status
        self.hit_over = hit_over
        self.inputs = []
        self.samples = []
        self.held_until = 0
        self.status_calls = 0

    def status(self):
        self.status_calls += 1
        return {"vsync": self.vsync}

    def run(self, vsyncs):
        assert vsyncs == 1
        self.vsync += self.step
        return {"vsync": self.vsync} if self.returned_status else None

    def call(self, method, **params):
        if method == "input":
            assert params == {"buttons": ["circle"], "vsyncs": 2}
            self.inputs.append(self.vsync)
            self.held_until = self.vsync + 2
            return {}
        assert method == "glows"
        self.samples.append((self.vsync, self.vsync <= self.held_until))
        return {"glows": [] if self.hit_over else [{"category": "effect"}],
                "categories": {"effects": {"energy": 1.0}}}


class FightSamplingTest(unittest.TestCase):
    def test_all_frames_and_original_input_timing(self):
        game = FakeGame()
        rows = fight_samples(game)
        self.assertEqual(len(rows), 510)
        self.assertEqual(game.inputs, [12000, 12102, 12204, 12306, 12408])
        self.assertEqual([v for v, held in game.samples], list(range(12001, 12511)))
        self.assertEqual([v for v, held in game.samples if held],
                         [start + delta for start in game.inputs for delta in (1, 2)])
        self.assertEqual(game.status_calls, 1)  # use returned stepping status

    def test_early_exit_keeps_original_iteration_boundary(self):
        game = FakeGame(hit_over=True)
        self.assertEqual(len(fight_samples(game)), 155)  # 151 iterations + 4 extra frames
        self.assertEqual(game.inputs, [12000, 12102])

    def test_rejects_non_unit_spacing(self):
        for step in (0, 2, 3):
            with self.subTest(step=step):
                game = FakeGame(step=step)
                with self.assertRaisesRegex(AssertionError, "one vsync apart"):
                    fight_samples(game)
                self.assertEqual(game.samples, [])

    def test_status_fallback(self):
        game = FakeGame(returned_status=False, hit_over=True)
        self.assertEqual(len(fight_samples(game)), 155)
        self.assertEqual(game.status_calls, 156)


if __name__ == "__main__":
    unittest.main()
