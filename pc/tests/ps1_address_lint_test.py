#!/usr/bin/env python3
"""ps1_address_lint: comments do not count, code does, and a rise fails."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import ps1_address_lint as lint  # noqa: E402


class Ps1AddressLintTest(unittest.TestCase):
    def test_comments_are_blanked_and_strings_kept(self):
        code = lint.strip_comments('int a; /* 0x80010000\n0x1F800000 */ int b; // D_80100000\nchar* s = "// x";\n')
        self.assertNotIn("0x80010000", code)
        self.assertNotIn("D_80100000", code)
        self.assertIn('"// x"', code)
        self.assertEqual(code.count("\n"), 3)

    def test_rules_match_what_they_name(self):
        for rule, text in [("ps1_address", "p = 0x80010000;"), ("scratchpad", "p = 0x1F800004;"),
                           ("unnamed_symbol", "D_800F4910 = vs_battle_collectEnvironmentActionTargets();"), ("guest_pointer_field", "psx_ptr(int) p;"),
                           ("guest_pointer_access", "psx_get(p->next)"), ("address_as_integer", "psx_to_int(u_int, p)"),
                           ("zig_raw_memory", "psx.byteAt(u32, p, 4)")]:
            self.assertTrue(lint.RULES[rule].search(text), rule)
        self.assertFalse(lint.RULES["ps1_address"].search("x = 0x8001;"))

    def test_only_a_rise_fails(self):
        rose, fell = lint.compare({"a": 5, "b": 2, "c": 3}, {"a": 4, "b": 3, "c": 3})
        self.assertEqual(rose, {"a": (4, 5)})
        self.assertEqual(fell, {"b": (3, 2)})


if __name__ == "__main__":
    unittest.main()
