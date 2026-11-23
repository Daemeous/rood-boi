/* A model's content key (model studio P3; pc/docs/plans/2026-11-22-model-studio-and-paint-channels-design.md 7.2): a hash of what the model IS
 * (its object header, vertex groups, vertices, polygon records) with the VRAM page it was loaded into left out, so one model has one key on
 * every machine, in every room and at every texture slot. The page enters a model only through the v byte of its texture coordinates (the loader
 * adds the texture slot's row * 64: parse_shp.zig fix_uvs), so each v is taken relative to the first polygon's page bits. Painted materials are
 * keyed by it ("m" + 10 hex digits in an object's text; the legacy 8 digit id, mesh_for's hash, still matches: model_aliases.json). */
#ifndef PC_MODEL_KEY_H
#define PC_MODEL_KEY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* The key: 40 bits (10 hex digits), never 0. kind: 0 flat, 1 coloured, 2 flat grouped, 3 coloured grouped (actor_native.c). obj: the object
 * record (bytes 0..7 header: group count, section counts); records: the polygon records, `words` of them as the packet builders walk them;
 * group_counts: the grouped builders' 12 words at the model (ignored for kinds 0 and 1). */
uint64_t pc_model_content_key(int kind, const unsigned char* obj, const unsigned* groups, unsigned group_count, const short* vertices,
    unsigned vertex_count, const unsigned* group_counts, const unsigned* records, const unsigned short counts[3]);

#ifdef __cplusplus
}
#endif
#endif
