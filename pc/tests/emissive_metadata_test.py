#!/usr/bin/env python3
"""CPU-only emissive metadata regression; no game, disc, GPU or shared build.

Compiles the production JSON and entry serialization functions in a temporary
translation unit, leaving renderer dependencies out. Run with:
    python3 pc/tests/emissive_metadata_test.py
Requires a C++17 compiler (CXX, default c++).
"""
import os
import pathlib
import shlex
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "pc/src"


def main():
    emitter = (SRC / "emissive.cpp").read_text()
    prefab = (SRC / "light_prefabs.cpp").read_text()
    production = emitter[emitter.index("static bool numbers("):emitter.index("static bool load_file(")]
    models = emitter[emitter.index("std::string model_key("):emitter.index("const std::vector<UseInfo> use_list(")]
    json_code = prefab[prefab.index("const Json* Json::get("):prefab.index("/* ---------------------------------------------------------- expressions")]
    program = r'''
#include "emissive_internal.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>
namespace prefab {
''' + json_code + '\n}\nnamespace emissive {\nusing prefab::Json;\n' + models + production + r'''
}
using namespace emissive;
using prefab::Json;
int main() {
    Json input;
    assert(prefab::json_parse(R"({"id":"metadata-test","scope":{"surfaces":[
      {"room":[28,14],"polygons":[100,200],"future":{"name":"first","nested":[null,false]}},
      {"room":[28,14],"polygons":[100],"future":{"name":"second"}},
      {"room":[28,14],"polygons":[],"future":{"name":"empty"}}
    ]}})", input));
    Entry e;
    assert(entry_from_json(input, e, nullptr));
    assert(e.scope.surfaces.size() == 2); // runtime membership stays deduplicated
    const std::string expected = prefab::json_write(*input.get("scope"));
    for (int i = 0; i < 3; ++i) {
        e.name = "unrelated editor change";
        Json output = entry_to_json(e);
        assert(prefab::json_write(*output.get("scope")) == expected);
        assert(entry_from_json(output, e, nullptr));
    }
    // Editing runtime membership must not resurrect a polygon from raw groups.
    e.scope.surfaces.erase(e.scope.surfaces.begin()); // remove polygon 100
    Json output = entry_to_json(e);
    const auto& groups = output.get("scope")->get("surfaces")->items;
    assert(groups.size() == 2);
    assert(groups[0].get("polygons")->items.size() == 1);
    assert(groups[0].get("polygons")->items[0].number == 200);
    assert(groups[1].get("polygons")->items.empty());
    Surface added;
    added.zone = 37; added.room = 0; added.polygon = 300;
    e.scope.surfaces.push_back(added);
    Entry round;
    assert(entry_from_json(entry_to_json(e), round, nullptr));
    assert(round.scope.surfaces.size() == 2);
    assert(std::find(round.scope.surfaces.begin(), round.scope.surfaces.end(), added) != round.scope.surfaces.end());
    // Empty metadata-only groups survive even when no runtime scope remains.
    e.scope.surfaces.clear();
    output = entry_to_json(e);
    assert(output.get("scope")->get("surfaces")->items.size() == 1);
    assert(output.get("scope")->get("surfaces")->items[0].get("future")->get("name")->string == "empty");
    assert(e.scope.empty()); // metadata alone does not restrict rendering
    // Explicit replacement/removal of the scope discards old optional fields.
    Json replacement;
    assert(prefab::json_parse(R"({"id":"metadata-test"})", replacement));
    assert(entry_from_json(replacement, e, nullptr));
    assert(!entry_to_json(e).get("scope"));
    puts("PASS: duplicate/empty surface metadata, repeated round trips, scope edits");
}
'''
    with tempfile.TemporaryDirectory(prefix="emissive-metadata-") as temp:
        source = pathlib.Path(temp) / "test.cpp"
        binary = pathlib.Path(temp) / "test"
        source.write_text(program)
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) +
                       ["-std=c++17", "-I", str(SRC), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
