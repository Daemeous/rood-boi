#!/usr/bin/env python3
"""Exercise verify's argv and compile-failure propagation without full builds."""
import ast
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
    def invoke_verify(self, options, failures=()):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "pc/tools").mkdir(parents=True)
            script = root / "pc/tools/verify.sh"
            shutil.copyfile(run.ROOT / "pc/tools/verify.sh", script)
            stub = root / "python3"
            stub.write_text(f"#!{sys.executable}\n"
                            "import json, os, sys\n"
                            "with open(os.environ['ARGV_LOG'], 'a') as f:\n"
                            "    f.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                            "sys.exit(7 if sys.argv[1:] in json.loads(os.environ['FAIL_CALLS']) else 0)\n")
            stub.chmod(0o755)
            log = root / "argv.jsonl"
            # A failed build may leave a stale binary. Verify must not remove it.
            stale = root / "pc/build/boot64/rood"
            stale.parent.mkdir(parents=True)
            stale.write_bytes(b"old binary fixture")
            env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}",
                       ARGV_LOG=str(log), FAIL_CALLS=json.dumps(failures),
                       ROOD_BUILD_LOCK="", ROOD_HEAVY_LOCK="")
            result = subprocess.run(["bash", str(script), *options], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(stale.read_bytes(), b"old binary fixture")
            return result, [json.loads(line) for line in log.read_text().splitlines()]

    def verify_calls(self, options):
        result, calls = self.invoke_verify(options)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return [call[1:] for call in calls if call[0] == "pc/tests/run.py"
                and "--wait-cool" not in call]

    def test_failed_build64_skips_dependent_suites(self):
        build = "pc/tools/build_boot.py"
        for options, windows, gl in [([], True, False),
                                     (["--gl"], True, True),
                                     (["--changed"], False, True),
                                     (["--changed", "--windows"], True, True),
                                     (["--no-windows", "--gl"], False, True)]:
            with self.subTest(options=options):
                result, calls = self.invoke_verify(options, [[build, "--arch", "64"]])
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertEqual([c for c in calls if c[0] == build],
                                 [[build], [build, "--battle"], [build, "--arch", "64"]]
                                 + ([[build, "--target", "windows"]] if windows else []))
                self.assertFalse([c for c in calls if c[0] == "pc/tests/run.py"
                                  and "--wait-cool" not in c])
                skipped = [line.split()[1] for line in result.stdout.splitlines()
                           if line.startswith("SKIP")]
                self.assertEqual(skipped, ["suite-64null", "suite-64vk"]
                                 + (["suite-64gl"] if gl else []))
                self.assertIn("1 failed", result.stdout)

    def test_independent_failures_are_counted(self):
        build = "pc/tools/build_boot.py"
        result, calls = self.invoke_verify(["--gl"],
                                          [[build], [build, "--battle"],
                                           [build, "--target", "windows"]])
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        suites = [c for c in calls if "--renderer" in c]
        self.assertEqual([c[c.index("--renderer") + 1] for c in suites],
                         ["null", "vulkan", "gl"])
        self.assertIn("3 failed", result.stdout)

        result, calls = self.invoke_verify([], [[build, "--arch", "64"],
                                                [build, "--target", "windows"]])
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn([build, "--target", "windows"], calls)
        self.assertIn("2 failed", result.stdout)

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

    def test_suite_failure_does_not_stop_other_checks(self):
        _, calls = self.invoke_verify(["--gl"])
        null_call = next(c for c in calls if "--renderer" in c and "null" in c)
        result, failed_calls = self.invoke_verify(["--gl"], [null_call])
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(failed_calls, calls)
        self.assertIn("1 failed", result.stdout)

    def test_emissive_changed_selection(self):
        inputs = ["pc/assets/emissive/catalog.json", "pc/tools/generate_lighting.py"]
        smoke = {"emissive_smoke", "fixture_lighting_smoke", "light_actors_smoke"}
        for renderer in ("null", "vulkan", "gl"):
            for path in inputs:
                with self.subTest(renderer=renderer, path=path):
                    argv = ["run.py", "--renderer", renderer, "--list", "--changed-files", path]
                    with patch.object(sys, "argv", argv), \
                         contextlib.redirect_stdout(io.StringIO()) as output:
                        self.assertEqual(run.main(), 0)
                    text = output.getvalue()
                    self.assertNotIn("the full suite", text)
                    self.assertNotIn("no suite for", text)
                    # Listed suites have a timeout; the preceding dependency report does not.
                    listed = {line.split()[0] for line in text.splitlines()
                              if len(line.split()) > 1 and line.split()[1].endswith("s")
                              and line.split()[1][:-1].isdigit()}
                    self.assertIn("emissive_catalog", listed)
                    if renderer == "null":
                        self.assertTrue(smoke.isdisjoint(listed))
                    else:
                        self.assertTrue(smoke <= listed, listed)

        suites = run.suites(run.default_args())
        for path in ("pc/tools/fixture_lib.py", "pc/tools/outdoor.py",
                     "lighting/default/generated/zone009_room000.txt"):
            with self.subTest(dependency=path):
                selection = affected.select([path], suites)
                self.assertFalse(selection.full)
                self.assertIn("emissive_catalog", selection.reasons)

    def test_parser_explicit_and_default_base(self):
        for argv, base in [(["--changed", "--", *NULL_SUITES], "main"),
                           (["--changed=HEAD~1", *NULL_SUITES], "HEAD~1"),
                           (["--changed", "HEAD~1", "--", *NULL_SUITES], "HEAD~1")]:
            with self.subTest(argv=argv):
                args = run.make_parser().parse_args(argv)
                self.assertEqual(args.changed, base)
                self.assertEqual(args.patterns, NULL_SUITES)


class CompileFailureTest(unittest.TestCase):
    """Run the real stamp invalidation and LP64 gate without game/toolchain data.

    build_boot is a top-level script, so extract these statements by structure
    rather than importing it (which would start a full build). Everything runs
    in a private tree, including verify's logs and a deliberately stale binary.
    """

    def harness(self, root, failed=True, windows=False):
        source = run.ROOT / "pc/tools/build_boot.py"
        tree = ast.parse(source.read_text())
        invalidate = next(node for node in tree.body
                          if isinstance(node, ast.Expr)
                          and ast.unparse(node) == "build_stamp.invalidate()")
        gate = next(node for node in tree.body
                    if isinstance(node, ast.If) and ast.unparse(node.test) == "arch64"
                    and any(isinstance(child, ast.ImportFrom) and child.module == "lp64_report"
                            for child in node.body))
        self.assertLess(invalidate.lineno, gate.lineno)
        script = root / "compile_gate.py"
        script.write_text(
            "import pathlib, sys\n"
            f"sys.path.insert(0, {str(run.ROOT / 'pc/tools')!r})\n"
            "from build_cache import Stamp\n"
            f"root = pathlib.Path({str(root)!r})\n"
            "out = root / 'out'\n"
            "out.mkdir(exist_ok=True)\n"
            "binary = out / 'rood_battle_probe'\n"
            "binary.write_text('old binary')\n"
            "build_stamp = Stamp(root, out, {})\n"
            "build_stamp.write([], [], [binary])\n"
            "assert build_stamp.up_to_date([], [])\n"
            f"arch64 = True\nwin = {windows!r}\n"
            f"arch64_results = [('c++', 'pc/src/lights.cpp', {int(failed)}, "
            "'pc/src/lights.cpp:1:1: error: regression failure' if "
            f"{failed!r} else '')]\n"
            + ast.unparse(invalidate) + "\n" + ast.unparse(gate) + "\n"
            # Represents the downstream link/finish: must be unreachable on failure.
            "binary.write_text('fresh binary')\n"
            "build_stamp.write([], [], [binary])\n")
        return script

    def assert_failed_artifacts(self, root):
        out = root / "out"
        self.assertEqual((out / "rood_battle_probe").read_text(), "old binary")
        self.assertFalse((out / "build-stamp.json").exists())
        report = json.loads((out / "report.json").read_text())
        self.assertFalse(report["files"][0]["compiled"])
        self.assertIn("lights.cpp", (out / "report.txt").read_text())
        # The same check used by build_boot --check cannot call this up to date.
        with patch.object(sys, "path", [str(run.ROOT / "pc/tools"), *sys.path]):
            from build_cache import Stamp
            self.assertFalse(Stamp(root, out, {}).up_to_date([], []))

    def test_compile_gate(self):
        for windows, failed in [(False, True), (True, True), (False, False)]:
            with self.subTest(windows=windows, failed=failed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                result = subprocess.run([sys.executable, str(self.harness(root, failed, windows))],
                                        capture_output=True, text=True)
                if failed:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("units failed to compile", result.stderr)
                    self.assert_failed_artifacts(root)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertTrue((root / "out/build-stamp.json").exists())
                    self.assertEqual((root / "out/rood_battle_probe").read_text(), "fresh binary")

    def test_verify_rejects_failed_linux64_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            harness = self.harness(root)
            (root / "pc/tools").mkdir(parents=True)
            script = root / "pc/tools/verify.sh"
            shutil.copyfile(run.ROOT / "pc/tools/verify.sh", script)
            stub = root / "python3"
            stub.write_text(f"#!{sys.executable}\n"
                            "import os, sys\n"
                            "if sys.argv[1:] == ['pc/tools/build_boot.py', '--arch', '64']:\n"
                            f"    os.execv(sys.executable, [sys.executable, {str(harness)!r}])\n")
            stub.chmod(0o755)
            env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}",
                       ROOD_BUILD_LOCK="", ROOD_HEAVY_LOCK="")
            result = subprocess.run(["bash", str(script), "--no-windows"], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertRegex(result.stdout, r"FAIL\s+build-64")
            self.assertNotIn("all passed", result.stdout)
            self.assert_failed_artifacts(root)


if __name__ == "__main__":
    unittest.main()
