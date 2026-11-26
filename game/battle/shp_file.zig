//! The actor shape file (SHP), decoded. The file is read by native_model's pc_nm_shp_layout (the layout below is the whole format); parseShp
//! works from this record instead of a cursor. The payload stays in the file's heap buffer, raw, where the model copy,
//! the sequence loader and LoadImage want it: the record holds pointers into it. Nothing is kept between parseShp's two passes
//! (the second pass only shifts polygons already copied), so the record is a value on the caller's stack, not an object of the
//! port arena.
//!
//!   +0x00  4 bytes (not read)
//!   +0x04  i32 nBones, i32 nQuads
//!   +0x0C  0xE8 bytes: the texture map overlays (copied into the actor)
//!   +0xF4  i32 model size N, then N bytes: the model (polygons at its polyOffset)
//!   then   the sequence block: 4 bytes, i32 length L, L bytes (what _loadSeq reads from its start)
//!   then   the texture block: i32 size S, then S bytes:
//!            S == 4:  i32 id of a loaded actor that shares this one's texture (nothing else follows in the block)
//!            else:    u8 flags (low bit: wide palette; high nibble: unk8_3), u8 width/2, u8 height/2, u8 palette colours C,
//!                     C words of palettes, then width * height bytes of texture (width and height as the bytes times 2)
const c = @import("c_30DB0");

pub const ShpFile = struct {
    n_bones: c_int,
    n_quads: c_int,
    /// The 0xE8 bytes of texture map overlays.
    overlays: [*]const u8,
    model: [*]const u8,
    model_size: c_int,
    /// The start of the sequence block, as _loadSeq takes it.
    seq: [*]const u8,
    /// The texture block's size word.
    texture_block_size: c_int,
    /// With a texture block of size 4: the model id of the actor whose texture this one shares.
    shared_model_id: c_int,
    flags: u8,
    width: c_int,
    height: c_int,
    clut_count: u8,
    cluts: [*]const u8,
    /// Past the palettes: width * height bytes.
    texture: [*]const u8,

    pub fn shares(self: *const ShpFile) bool {
        return self.texture_block_size == 4;
    }
};

/// Reads the file at `at` (its address in the buffer): the layout is native_model's (pc_nm_shp_layout, the one decoder rood-studio reads the same files with).
pub fn decode(at: usize) ShpFile {
    var parts: c.pc_nm_shp_parts = undefined;
    c.pc_nm_shp_layout(@ptrFromInt(at), &parts);
    return .{
        .n_bones = parts.n_bones,
        .n_quads = parts.n_quads,
        .overlays = parts.overlays,
        .model = parts.model,
        .model_size = parts.model_size,
        .seq = parts.seq,
        .texture_block_size = parts.texture_block_size,
        .shared_model_id = parts.shared_model_id,
        .flags = parts.flags,
        .width = parts.width,
        .height = parts.height,
        .clut_count = parts.clut_count,
        .cluts = parts.cluts,
        .texture = parts.texture,
    };
}
