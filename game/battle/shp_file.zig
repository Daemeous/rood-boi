//! The actor shape file (SHP), decoded. The file is read in one place, here (the layout below is the whole format); parseShp
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
const std = @import("std");

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

inline fn i32At(p: [*]const u8) c_int {
    return @as(*align(1) const c_int, @ptrCast(p)).*;
}

/// Reads the file at `at` (its address in the buffer).
pub fn decode(at: usize) ShpFile {
    const base: [*]const u8 = @ptrFromInt(at);
    var f: ShpFile = undefined;
    f.n_bones = i32At(base + 4);
    f.n_quads = i32At(base + 8);
    f.overlays = base + 0xC;
    f.model_size = i32At(base + 0xF4);
    f.model = base + 0xF8;
    f.seq = f.model + @as(usize, @as(u32, @bitCast(f.model_size)));
    const seq_length = i32At(f.seq + 4);
    const block = f.seq + 8 + @as(usize, @as(u32, @bitCast(seq_length)));
    f.texture_block_size = i32At(block);
    const body = block + 4;
    if (f.texture_block_size == 4) {
        f.shared_model_id = i32At(body);
        f.flags = 0;
        f.width = 0;
        f.height = 0;
        f.clut_count = 0;
        f.cluts = body;
        f.texture = body;
    } else {
        f.shared_model_id = 0;
        f.flags = body[0];
        f.width = @as(c_int, body[1]) * 2;
        f.height = @as(c_int, body[2]) * 2;
        f.clut_count = body[3];
        f.cluts = body + 4;
        f.texture = f.cluts + @as(usize, f.clut_count) * 4;
    }
    return f;
}
