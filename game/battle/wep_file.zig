//! The weapon model file (WEP), decoded. The file is a header, an offsets block, a palette header, the palettes and the
//! texture, then the bone info; its fields are read once, here, from the bytes (the layout below is the whole format) and
//! parseWep works from this record instead of a cursor. The payload (the offsets block with its polygons, the palettes,
//! the texture, the bone info) stays in the file's buffer, raw, where the renderer and LoadImage want it: the record
//! holds pointers into it. The record lives in the port arena, not the game heap, from the first pass to the second.
//!
//!   +0     4 bytes (not read)
//!   +4     i32 nBones, i32 nQuads (stored into the model slot as the C's two int stores)
//!   +0xC   i32 offsets size N, then N bytes: the model's offsets block (polygons at polygonsOffset)
//!   +0x10+N   5 bytes (not read), u8 texture width in 4-byte units (x2 = texels per row), 1 byte (not read),
//!          u8 palette size (96: double palette, else 48), then the first palette (w*2 bytes, w 32 or 16)
//!          and 7 palettes of (64 or 32)*2 bytes, then the texture (texW * 32 bytes), then the bone info.
const common = @import("common");
const psx = common.psx;
const port_arena = common.port_arena;

pub const WepFile = extern struct {
    n_bones: c_int,
    n_quads: c_int,
    /// The offsets block's size in bytes.
    offsets_size: c_int,
    /// Texels per row of the texture (the file's byte times 2).
    tex_width: c_uint,
    /// The file's palette size byte: 96 is a double palette.
    palette_size: u8,
    offsets: [*]const u8,
    /// The first palette (16 or 32 colours).
    palette_first: [*]const u8,
    /// The seven palettes that follow it.
    palettes: [*]const u8,
    texture: [*]const u8,
    /// The bone info after the texture: 8 bytes per bone.
    bone_info: [*]const u8,

    pub fn doublePalette(self: *const WepFile) bool {
        return self.palette_size == 96;
    }
    /// Bytes of the first palette.
    pub fn firstPaletteBytes(self: *const WepFile) usize {
        return if (self.doublePalette()) 64 else 32;
    }
    /// Bytes of each of the seven palettes.
    pub fn paletteBytes(self: *const WepFile) usize {
        return if (self.doublePalette()) 128 else 64;
    }
};

inline fn i32At(p: [*]const u8) c_int {
    return @as(*align(1) const c_int, @ptrCast(p)).*;
}

/// Decodes the file at `at` (its address in the buffer) into the WEP record of the port arena (replacing the one a
/// load that never finished left). Null when the arena is full.
pub fn decode(at: usize) ?*WepFile {
    const file = port_arena.put(WepFile, .wep) orelse return null;
    const base: [*]const u8 = @ptrFromInt(at);
    file.n_bones = i32At(base + 4);
    file.n_quads = i32At(base + 8);
    file.offsets_size = i32At(base + 0xC);
    const size: usize = @as(u32, @bitCast(file.offsets_size));
    file.offsets = base + 0x10;
    const after = base + 0x10 + size;
    file.tex_width = @as(c_uint, after[5]) *% 2;
    file.palette_size = after[7];
    file.palette_first = after + 8;
    file.palettes = file.palette_first + file.firstPaletteBytes();
    file.texture = file.palettes + 7 * file.paletteBytes();
    file.bone_info = file.texture + @as(usize, file.tex_width) * 32;
    return file;
}

/// The record the first pass made (the second pass finds it in the arena).
pub fn current() ?*WepFile {
    return port_arena.get(WepFile, .wep);
}

/// Done with it.
pub fn release() void {
    port_arena.clear(WepFile, .wep);
}
