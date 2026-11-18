//! TIM image header parsing for battle textures (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
//!
//! The TIM file (the layout is the whole format; every field is a little-endian word, any alignment):
//!   +0   u32 magic (not read)
//!   +4   u32 mode; bit 3 set: a palette block follows
//!   +8   the palette block when bit 3 is set: u32 size in bytes, RECT (8 bytes) at +4, the palette's pixels from +12 (size bytes in all)
//!   then the image block, the same way: u32 size, RECT at +4, pixels from +12
//! The record holds the mode and pointers into the blob (the rectangles and the pixels stay where they are, for LoadImage).
const psx = @import("common").psx;
const v = @import("views_146C");

inline fn word(p: [*]const u8) u32 {
    return @as(*align(1) const u32, @ptrCast(p)).*;
}

/// orig: vs_battle_parseTim (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills a TIM_IMAGE's mode, optional CLUT rect and address, and pixel rect and address from a TIM blob.
pub fn parseTim(data: [*c]c_uint, tim: ?*v.TIM_IMAGE) callconv(.c) void {
    const t = tim.?;
    var block: [*]u8 = @as([*]u8, @ptrCast(data)) + 8; // past the magic and the mode
    t.mode = word(@as([*]u8, @ptrCast(data)) + 4);

    if ((t.mode & 8) != 0) {
        psx.put(&t.crect, @as(?*v.RECT, @ptrCast(@alignCast(block + 4))));
        psx.put(&t.caddr, @as(?*u32, @ptrCast(@alignCast(block + 12))));
        block += word(block) & ~@as(u32, 3); // the block's size in bytes, as the C counts it: words (arg0 += *arg0 >> 2)
    } else {
        psx.put(&t.crect, null);
        psx.put(&t.caddr, null);
    }

    psx.put(&t.prect, @as(?*v.RECT, @ptrCast(@alignCast(block + 4))));
    psx.put(&t.paddr, @as(?*u32, @ptrCast(@alignCast(block + 12))));
}
