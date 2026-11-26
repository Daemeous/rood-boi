/* The file formats' layouts as plain C (no includes: the game's own headers take it too): where the parts of a model file are, from its bytes alone, and
 * the fix-up the loaders make of its polygon records. One reading of each format, shared by the game's loaders (game/battle) and rood-studio's decoders
 * (native_model.h); pc/docs/plans/2026-11-24-standalone-studio-design.md, "the seam for the game's loaders". The files are trusted to be whole here;
 * the decoders check them. */
#ifndef ROOD_NATIVE_MODEL_FORMATS_H
#define ROOD_NATIVE_MODEL_FORMATS_H

/* A WEP file's layout: where its parts are, from its bytes alone (no size, no copy: what a loader walking the file's buffer needs). The file is
 * trusted to be whole; pc_nm_wep_decode checks it. */
typedef struct {
    int n_bones, n_quads;
    unsigned offsets_size, tex_width;
    int palette_size;                   /* the file's byte: 96 is a double palette */
    unsigned first_palette_bytes, palette_bytes;
    const unsigned char *offsets, *palette_first, *palettes, *texture, *bone_info;
} pc_nm_wep_parts;
void pc_nm_wep_layout(const unsigned char* file, pc_nm_wep_parts* out);
/* A model's polygon records with a texture slot's columns and rows added to every texture coordinate, in place (the loaders' fix-up); `kind` PC_NM_FLAT or PC_NM_COLOURED. */
void pc_nm_fix_records(void* records, int triangles, int quads, int others, int kind, int u_add, int v_add);


/* An SHP file (an actor's shape): the counts, the texture map overlays, the model block, the sequence block's start and the texture block, from its bytes alone. A texture block of
 * size 4 names the model whose texture this one shares (shared_model_id); else flags (bit 0: the wide palette, the high nibble: the coloured records), the texture's width and
 * height in bytes and rows, the palette's colour count, the palettes and the texels. */
typedef struct {
    int n_bones, n_quads;
    const unsigned char *overlays, *model;
    int model_size;
    const unsigned char* seq;
    int texture_block_size, shared_model_id;
    unsigned char flags;
    int width, height;
    unsigned char clut_count;
    const unsigned char *cluts, *texture;
} pc_nm_shp_parts;
void pc_nm_shp_layout(const unsigned char* file, pc_nm_shp_parts* out);

/* A ZUD file (an enemy's package, loaded by the zone's enemy table and by the ENEMY menu): a 0x30-byte header, then the parts it names by byte offset from the file's start.
 *   +0x00 u8 characterId, weapon, weaponCategory, weaponMaterial, shield, shieldMaterial, defaultMaterial, (pad)
 *   +0x08 u32 offset, i32 size of each part, in this order: shp (the model), weaponWep, shieldWep, commonSeq, battleSeq
 * The parts stay raw in the file's buffer, where the object loader (vs_battle_populateDataSlot) reads them; each part's pointer is the file's address plus its offset. */
typedef struct {
    void* data;
    int size;
} pc_nm_zud_part;
typedef struct {
    unsigned char characterId, weapon, weaponCategory, weaponMaterial, shield, shieldMaterial, defaultMaterial;
    pc_nm_zud_part shp, weaponWep, shieldWep, commonSeq, battleSeq;
} pc_nm_zud;
void pc_nm_zud_decode(pc_nm_zud* out, const void* file);

/* A sequence block (the SHP's, a ZUD's common and battle parts, a weapon's and the effects'): u8 head (the animation count; the weapon kind: its animation count), u16 bones (0: any),
 * i32 size, then the data. A table in the data: u32 data offset, u32 index offset, the animation records (nBones * 4 + 10 bytes each) from +8, the index (a byte for each numbered
 * animation: its record, 0xFF none) and the animation streams. A shared set: i32 count N, N u16 animation ids and N u16 keys (4 bytes each), N u32 stream offsets counted from the end of the tables. */
typedef struct {
    unsigned char head;
    unsigned short bones;
    int size;
    const unsigned char* data;
} pc_nm_seq_block;
void pc_nm_seq_block_at(const unsigned char* at, pc_nm_seq_block* out);
typedef struct {
    int count;
    const unsigned char* base;
} pc_nm_seq_shared;
void pc_nm_seq_shared_at(const unsigned char* at, pc_nm_seq_shared* out);
unsigned short pc_nm_seq_shared_id(const pc_nm_seq_shared* set, int i);
unsigned short pc_nm_seq_shared_key(const pc_nm_seq_shared* set, int i);
unsigned pc_nm_seq_shared_offset(const pc_nm_seq_shared* set, int i);
const unsigned char* pc_nm_seq_shared_streams(const pc_nm_seq_shared* set); /* where the offsets count from */
/* A table's animation `idx`: its record's byte offset in the table (from the table's start), -1 when the index holds 0xFF; *record gets the index byte. An `idx` below zero reads
 * before the index, as the game's does. */
int pc_nm_table_record(const unsigned char* table, int bones, int idx, int* record);

/* One animation track (vs_actors_decodeAnimationTrack's stream): the first vector's value, then packets of frame deltas. The track's state is this struct:
 * the position in the stream (`pos`), the frame the current delta lasts to (`next`), the delta and the value. `stream` is the track's own bytes (the table's data
 * from the track's offset); `length` bounds the reads. pc_nm_track_step applies one frame: it reads the packets the frame needs, then adds the delta to the value
 * (`rotation`: the turns keep 13 bits). It sets `ended` (nothing applied) when the stream ends in a zero byte. */
typedef struct {
    const unsigned char* stream;
    unsigned length;
    unsigned pos;
    int next;
    short delta[3], val[3];
    int live;  /* it has packets to read */
    int ended;
} pc_nm_track;
void pc_nm_track_step(pc_nm_track* t, int frame, int rotation);
/* The table's data (the animation streams): the table's first word is its offset. */
const unsigned char* pc_nm_table_data(const unsigned char* table);
/* A first vector: three big-endian halves, each doubled (the turns and the root). */
void pc_nm_initial_vector(const unsigned char* at, short out[3]);

#endif
