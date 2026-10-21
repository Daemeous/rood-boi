#!/usr/bin/env python3
"""The one sanitiser for text sent to an external model (spec_bakeoff.py,
port_pipeline.py): prompts, compiler errors, harness output, retry
feedback. Nothing local leaves the machine: the checkout's path becomes
relative, the Zig cache (toolchain and global cache) becomes `<zig-cache>`,
scratch directories (/tmp/..., $TMPDIR) become `<tmp>`, any other path
under the home directory becomes `~/...`, and the user's name becomes
`<user>`.

    import prompt_privacy
    text = prompt_privacy.sanitise(text)        # before every call
    prompt_privacy.check(text)                  # raises Leak if anything local is left

`check` is the guard each caller runs on the exact request body; a test
(pc/tests/port_pipeline_test.py) fails if a prompt contains the home path.
"""
import getpass
import os
import pathlib
import re
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


class Leak(Exception):
    pass


def _user():
    try:
        return getpass.getuser()
    except Exception:
        return os.environ.get("USER") or os.environ.get("USERNAME") or ""


def _roots():
    """(path, replacement) pairs, longest path first."""
    pairs = []
    sys.path.insert(0, str(ROOT / "pc/tools"))
    try:
        import zig_toolchain
        pairs.append((str(zig_toolchain.global_cache()), "<zig-cache>/global"))
        pairs.append((str(zig_toolchain.cache_root()), "<zig-cache>"))
    except Exception:
        pass
    # The main checkout (a worktree's common dir) and this checkout.
    main = ROOT
    git = ROOT / ".git"
    if git.is_file():
        m = re.match(r"gitdir: (.*)", git.read_text().strip())
        if m:
            main = pathlib.Path(m.group(1)).resolve().parents[2]
    for root in {ROOT, main, ROOT.resolve(), main.resolve()}:
        pairs.append((str(root) + "/", ""))
        pairs.append((str(root), "."))
    tmp = tempfile.gettempdir()
    for t in {tmp, os.path.realpath(tmp), "/tmp"}:
        pairs.append((t + "/", "<tmp>/"))
    home = str(pathlib.Path.home())
    pairs.append((home, "~"))
    return sorted(set(pairs), key=lambda p: -len(p[0]))


_PAIRS = None


def sanitise(text):
    """`text` with every local path and the user's name replaced."""
    global _PAIRS
    if text is None:
        return None
    if _PAIRS is None:
        _PAIRS = _roots()
    for path, replacement in _PAIRS:
        text = text.replace(path, replacement)
    # A scratch directory's own name (tmpXXXX, or an agent scratch tree) is local too.
    # (its last component, a file name, stays: `ref.c:12` is still useful).
    text = re.sub(r"<tmp>/claude-\d+(/[^\s'\"`:,)\]]*)?",
                  lambda m: "<tmp>/.../" + m.group(0).rstrip("/").rsplit("/", 1)[-1] if m.group(1) else "<tmp>", text)
    user = _user()
    if user and len(user) > 2:
        text = re.sub(re.escape(user), "<user>", text, flags=re.I)
    return text


def problems(text):
    """What is still local in `text` (empty when clean)."""
    found = []
    home = str(pathlib.Path.home())
    if home and home != "/" and home in text:
        found.append(f"the home directory ({home})")
    for path in {str(ROOT), os.path.realpath(tempfile.gettempdir()) + "/claude"}:
        if path in text:
            found.append(path)
    scratch = re.escape(os.path.join(tempfile.gettempdir(), "claude"))  # the agents' scratch, any user
    if re.search(scratch + r"[-/]", text):
        found.append("the agents' temporary scratch directory")
    user = _user()
    if user and len(user) > 2 and re.search(re.escape(user), text, re.I):
        found.append("the user name")
    for path, replacement in _PAIRS or _roots():
        if replacement.startswith("<zig-cache>") and path in text:
            found.append("the Zig cache path")
    return found


def check(text):
    """Raise Leak when `text` still holds anything local."""
    found = problems(text)
    if found:
        raise Leak("prompt_privacy: refusing to send text containing " + ", ".join(found))
    return text


if __name__ == "__main__":
    data = sys.stdin.read()
    sys.stdout.write(sanitise(data))
