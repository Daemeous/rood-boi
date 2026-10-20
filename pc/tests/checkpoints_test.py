#!/usr/bin/env python3
"""CPU-only mocked regressions for checkpoint launch cleanup."""
import pathlib
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checkpoints


class LaunchAtCleanupTest(unittest.TestCase):
    def assert_original_error(self, operation, game, error):
        game.close.side_effect = RuntimeError("close also failed")
        with self.assertRaises(RuntimeError) as raised:
            operation()
        self.assertIs(raised.exception, error)
        game.close.assert_called_once_with()

    def test_fresh_boot_drive_failure_closes_game_and_reraises(self):
        game = Mock()
        error = RuntimeError("drive failed")
        with patch.object(checkpoints, "prepare", return_value=None), \
             patch.object(checkpoints.Game, "launch", return_value=game) as launch, \
             patch.object(checkpoints, "_drive", side_effect=error) as drive:
            self.assert_original_error(lambda: checkpoints.launch_at("title", fast=True), game, error)

        launch.assert_called_once_with(fast=True)
        drive.assert_called_once_with(game, "title", checkpoints.TITLE_VSYNC)

    def test_snapshot_warmup_failure_closes_game_and_reraises(self):
        game = Mock()
        error = RuntimeError("warm-up failed")
        snapshot = pathlib.Path("cached-title.snap")
        game.run.side_effect = error
        with patch.object(checkpoints, "prepare", return_value=snapshot), \
             patch.object(checkpoints.Game, "launch", return_value=game) as launch:
            self.assert_original_error(lambda: checkpoints.launch_at("title", fast=True), game, error)

        launch.assert_called_once_with(snapshot=snapshot, fast=True)
        game.run.assert_called_once_with(checkpoints.WARMUP)


if __name__ == "__main__":
    unittest.main()
