#!/usr/bin/env python3
"""Exercise verify's argv without builds, thermal waits, or game processes."""
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import affected
import run


NULL_SUITES = ["null_backend_smoke", "shadow_battle_smoke", "ownership_smoke",
               "battle_corpus_candidate", "zig_hotswap_smoke", "trial_corpus", "classic_parity"]


class VerifyArgsTest(unittest.TestCase):
    def verify_calls(self, options):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "pc/tools").mkdir(parents=True)
            script = root / "pc/tools/verify.sh"
            shutil.copyfile(run.ROOT / "pc/tools/verify.sh", script)
            stub = root / "python3"
            stub.write_text(f"#!{sys.executable}\n"
                            "import json, os, sys\n"
                            "with open(os.environ['ARGV_LOG'], 'a') as f:\n"
                            "    f.write(json.dumps(sys.argv[1:]) + '\\n')\n")
            stub.chmod(0o755)
            log = root / "argv.jsonl"
            env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}",
                       ARGV_LOG=str(log), ROOD_BUILD_LOCK="", ROOD_HEAVY_LOCK="")
            subprocess.run(["bash", str(script), *options], env=env,
                           capture_output=True, text=True, check=True)
            return [call[1:] for line in log.read_text().splitlines()
                    if (call := json.loads(line))[0] == "pc/tests/run.py"
                    and "--wait-cool" not in call]

    def test_changed_null_patterns(self):
        for options, base in [(["--changed"], "main"),
                              (["--changed", "HEAD~1"], "HEAD~1"),
                              (["--changed", "--", "-j", "2"], "main")]:
            with self.subTest(options=options):
                calls = self.verify_calls(options)
                self.assertEqual(len(calls), 3)
                for argv in calls:
                    args = run.make_parser().parse_args(argv)
                    self.assertEqual(args.changed, base)
                    self.assertEqual(args.patterns,
                                     NULL_SUITES if args.renderer == "null" else [])
                    # A docs-only diff must be empty in every lane, and the
                    # revision passed to git must not be a suite name.
                    with patch.object(sys, "argv", ["run.py", *argv]), \
                         patch.object(affected, "changed_files", return_value=["pc/docs/Testing.md"]) as changed, \
                         contextlib.redirect_stdout(io.StringIO()) as output:
                        self.assertEqual(run.main(), 0)
                    changed.assert_called_once_with(base)
                    self.assertIn("nothing to run for this change", output.getvalue())

    def test_parser_explicit_and_default_base(self):
        for argv, base in [(["--changed", "--", *NULL_SUITES], "main"),
                           (["--changed=HEAD~1", *NULL_SUITES], "HEAD~1"),
                           (["--changed", "HEAD~1", "--", *NULL_SUITES], "HEAD~1")]:
            with self.subTest(argv=argv):
                args = run.make_parser().parse_args(argv)
                self.assertEqual(args.changed, base)
                self.assertEqual(args.patterns, NULL_SUITES)


if __name__ == "__main__":
    unittest.main()
