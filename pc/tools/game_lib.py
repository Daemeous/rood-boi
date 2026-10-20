#!/usr/bin/env python3
"""Build the development owned library (Debug/PIC, cached LLVM objects).

The caller holds ROOD_BUILD_LOCK. --source-root supports isolated smoke edits.
Publication is atomic; failed compilation never replaces a live library.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

import ownership
import zig_toolchain

ROOT = Path(__file__).resolve().parents[2]


def abi(table):
    return hashlib.sha256(repr(table.routes()).encode()).hexdigest()


def write_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def build(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    table = ownership.load(source / 'ownership.toml')
    zig = str(zig_toolchain.ensure())
    cache = ROOT / 'pc/build/dev64/zig-library-cache'
    with tempfile.TemporaryDirectory(prefix='game-lib-', dir=output.parent) as directory:
        # Stable module paths let Zig reuse its object cache across edits.
        # 0.17 self-hosted PIC emits broken unwind data; LLVM incremental
        # reports "TODO implement saving linker state for lld". Use cached
        # non-incremental LLVM until both support safe stack unwinding.
        work = cache / ('modules-' + hashlib.sha256(str(source).encode()).hexdigest()[:16])
        work.mkdir(parents=True, exist_ok=True)
        write_changed(work / 'ownership.zig', ownership.ownership_zig(table))
        objects = []
        for overlay in sorted({s.overlay for s in table.subsystems}):
            wrapper = work / (overlay + '.zig')
            write_changed(wrapper, 'pub const reload_mode = 2;\npub const panic = @import("impl").panic;\ncomptime { _ = @import("impl"); }\n')
            obj = work / (overlay + '.o')
            args = [zig, 'build-obj', '-fno-compiler-rt', '-femit-bin=' + str(obj),
                    '--cache-dir', str(cache), '--global-cache-dir', str(zig_toolchain.global_cache())]
            # The per-unit views/c modules the host build generated (build_boot.py
            # --engine-library): layouts and declarations are host-side facts.
            listed = ROOT / 'pc/build/dev64/zig' / (overlay + '_modules.json')
            generated = json.loads(listed.read_text()) if listed.exists() else {}
            modules = [('root', wrapper, ['impl']),
                       ('impl', source / overlay / 'root.zig', ['common', 'ownership', *sorted(generated)]),
                       ('common', source / 'common/common.zig', []), ('ownership', work / 'ownership.zig', [])]
            modules += [(name, Path(path), deps) for name, (path, deps) in sorted(generated.items())]
            for name, path, deps in modules:
                # -fno-stack-check as the static objects: the safety panics' message
                # buffers would otherwise call __zig_probe_stack, which no host has.
                args += ['-target', 'x86_64-linux-gnu', '-mcpu', 'baseline', '-O', 'Debug', '-fPIC',
                         '-fllvm', '-funwind-tables', '-fno-stack-check']
                for dep in deps:
                    args += ['--dep', dep]
                args += [f'-M{name}={path}']
            subprocess.run(args, check=True, cwd=ROOT)
            sections = subprocess.check_output(['readelf', '-SW', str(obj)], text=True)
            for line in sections.splitlines():
                section = re.match(r'\s*\[\s*\d+\]\s+(\.(?:data|bss|tdata|tbss)(?:\.\S+)?)\s+\w+\s+\w+\s+\w+\s+(\w+)', line)
                if section and int(section[2], 16) and not section[1].startswith('.data.rel.ro'):
                    raise SystemExit(f'{obj.name}: owned code keeps no mutable statics ({section[1]}); '
                                     'keep state in guest memory or host C')
            objects.append(str(obj))
        metadata = work / 'abi.c'
        metadata.write_text(f'const char rood_game_abi[] = "{abi(table)}";\n')
        exports = work / 'exports.map'
        exports.write_text('{ global: rood_game_abi; ' + ' '.join(n + '__candidate; ' + n + '__candidate_abi;' for n, _ in table.routes()) + ' local: *; };\n')
        candidate = Path(directory) / 'librood_game.so'
        subprocess.run(['gcc', '-shared', '-fPIC', '-Wl,--version-script=' + str(exports),
                        *objects, str(metadata), '-o', str(candidate)], check=True)
        os.replace(candidate, output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT / 'game')
    parser.add_argument('--out', type=Path, default=ROOT / 'pc/build/dev64/librood_game.so')
    args = parser.parse_args()
    print(build(args.source_root, args.out))


if __name__ == '__main__':
    main()
