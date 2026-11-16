"""The names the game's symbols were given after the decomp (pc/names/symbols.tsv).

The reference ELFs and config/ (the decomp's own tree, src/) keep the decomp's names (func_8009506C, vs_battle_aiActorAiStates);
our code (pc/src, game/) uses the new ones. The build reads the reference through renamer(), which turns an
original name into ours, so every address lookup finds the symbol under the name the code uses.
Written by pc/tools/import_names.py.
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SYMBOLS = ROOT / "pc/names/symbols.tsv"


def load(path=SYMBOLS):
    """{original name: new name} (columns: kind, overlay, original, new, confidence, source)."""
    out = {}
    if pathlib.Path(path).exists():
        for line in pathlib.Path(path).read_text().splitlines():
            if line and not line.startswith("#"):
                cols = line.split("\t")
                out[cols[2]] = cols[3]
    return out


def renamer(path=SYMBOLS):
    """A function name -> name that maps an original name to ours and leaves any other as it is."""
    table = load(path)
    return lambda name: table.get(name, name)


def canonicaliser(path=SYMBOLS):
    """A function text -> text that turns every new name back into the decomp's (an object's symbols, sections and
    relocation targets), so what is hashed does not depend on the names: the save states' keys
    (snapshot_layout.py) stay what they were before a rename. A name inside a longer one (ending_<name>,
    <name>__battle) counts; a name inside a word does not."""
    import re
    back = {new: old for old, new in load(path).items()}
    if not back:
        return lambda text: text
    pattern = re.compile(r"(?<![A-Za-z0-9])(?:" + "|".join(re.escape(n) for n in sorted(back, key=len, reverse=True))
                         + r")(?![A-Za-z0-9])")
    return lambda text: pattern.sub(lambda m: back[m.group(0)], text)
