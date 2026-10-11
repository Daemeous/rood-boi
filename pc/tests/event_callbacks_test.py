"""Exercise generated callbacks and reject unresolvable or incompatible targets."""

import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from event_callbacks import generate_event_callbacks, generate_effect_callbacks


class EventCallbacksTest(unittest.TestCase):
    def setUp(self):
        self.overlay = bytearray(0x800E9D30 - 0x80068800 + 256 * 4)
        struct.pack_into(
            "<256I",
            self.overlay,
            len(self.overlay) - 1024,
            *([0x100, 0x200, 0x300, 0x400] * 64),
        )
        self.symbols = "\n".join(
            f"1: {a:08x} 4 FUNC GLOBAL DEFAULT 1 {n}"
            for a, n in [
                (0x100, "two"),
                (0x200, "one"),
                (0x300, "zero"),
                (0x400, "named_handler"),
            ]
        )
        self.source = """
        typedef unsigned char u_char;
        int two(u_char* script, short slot) { return *script + slot; }
        int one(u_char* script) { return *script; }
        int zero(void) { return 42; }
        int named_handler(u_char* script, short slot __attribute__((unused))) { return *script + 1; }
        """

    def test_native_calls_preserve_opcode_order_and_arity(self):
        generated = generate_event_callbacks(
            self.overlay, self.symbols, self.source, ""
        )
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "probe.c").write_text(
                self.source
                + generated
                + """
            int main(void) {
                u_char script = 7;
                for (int i = 0; i < 256; i++) {
                    int expected[] = {10, 7, 42, 8};
                    if (_opcodeFunctionTable[i](&script, 3) != expected[i % 4]) return 1;
                }
                return 0;
            }
            """
            )
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "probe.c"),
                    "-o",
                    str(path / "probe"),
                ],
                check=True,
            )
            subprocess.run([str(path / "probe")], check=True)

    def test_unknown_target_rejected(self):
        struct.pack_into("<I", self.overlay, len(self.overlay) - 1024, 0xDEADBEEF)
        with self.assertRaisesRegex(ValueError, "Unknown event handler"):
            generate_event_callbacks(self.overlay, self.symbols, self.source, "")

    def test_incompatible_signature_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unsupported event handler signature"):
            generate_event_callbacks(
                self.overlay,
                self.symbols,
                self.source.replace("short slot)", "int slot)"),
                "",
            )

    def test_effect_commands_and_builtin_renderer(self):
        self.overlay = bytearray(0x800EC4B8 - 0x80068800)
        struct.pack_into("<49I", self.overlay, 0x800EC3F4 - 0x80068800, *([0x500] * 49))
        struct.pack_into("<I", self.overlay, 0x800EC324 - 0x80068800, 0x600)
        symbols = "1: 00000500 4 FUNC GLOBAL DEFAULT 1 command\n2: 00000600 4 FUNC GLOBAL DEFAULT 1 func_800CE174"
        source = """
        typedef struct { int value; } D_800F53B8_t;
        typedef int (*effectExec)(void*, unsigned, int);
        int command(D_800F53B8_t* node) { return node->value; }
        int func_800CE174(void* node, unsigned mode, int config) { (void)node; return mode + config; }
        """
        generated = generate_effect_callbacks(self.overlay, symbols, source)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "probe.c").write_text(
                source
                + generated
                + """
            int main(void) {
                D_800F53B8_t node = {37};
                for (int i = 0; i < 49; i++) if (D_800EC3F4[i](&node) != 37) return 1;
                return D_800EC324[0](&node, 2, 4) != 6;
            }
            """
            )
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "probe.c"),
                    "-o",
                    str(path / "probe"),
                ],
                check=True,
            )
            subprocess.run([str(path / "probe")], check=True)
        with self.assertRaisesRegex(
            ValueError, "Unknown or incompatible effect command"
        ):
            generate_effect_callbacks(
                self.overlay,
                symbols,
                source.replace("D_800F53B8_t* node)", "int* node)"),
            )
        struct.pack_into("<I", self.overlay, 0x800EC324 - 0x80068800, 0xDEADBEEF)
        with self.assertRaisesRegex(ValueError, "Built-in effect renderer"):
            generate_effect_callbacks(self.overlay, symbols, source)


if __name__ == "__main__":
    unittest.main()
