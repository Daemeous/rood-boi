#!/usr/bin/env python3
"""rt_skin.comp reads render.c's mesh vertices as words: its vertex stride and its polygon stride (four vertices)
must follow VK_MESH_VERTEX_BYTES (pc/src/vk_hw.h). Materials by object grew the vertex from 40 to 44 bytes and the
polygon stride kept 40 words/10: every polygon after the first read another polygon's texture page and flags, so the
GI lane drew differently from main with no object entry anywhere."""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
bytes_ = int(re.search(r"#define VK_MESH_VERTEX_BYTES (\d+)", (ROOT / "pc/src/vk_hw.h").read_text()).group(1))
shader = (ROOT / "pc/shaders/vulkan/rt_skin.comp").read_text()
words = bytes_ // 4
vertex = int(re.search(r"uint w = v \* (\d+)u;", shader).group(1))
polygon = int(re.search(r"uint w = poly \* (\d+)u;", shader).group(1))
assert bytes_ % 4 == 0 and vertex == words, f"rt_skin.comp vertex stride {vertex} words, VK_MESH_VERTEX_BYTES {bytes_} ({words})"
assert polygon == 4 * words, f"rt_skin.comp polygon stride {polygon} words, four vertices are {4 * words}"
print(f"rt_skin_stride_test: ok ({words} words per vertex, {polygon} per polygon)")
