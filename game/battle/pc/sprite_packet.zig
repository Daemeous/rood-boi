//! Sprite packets in the primitive buffer (D2: 573B8.c's inline assembly; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const c = @import("c_battle_helpers");

/// orig: vs_battle_setSprite (pc/src/battle_helpers.c)
/// Builds a sprite packet at the primitive cursor (scratchpad 0x1F800000,
/// through pc_sprite_packet with texture page 0 and linked to `next`),
/// advances the cursor past it (6 words) and returns the packet.
pub fn setSprite(brightness: c_int, xy: c_int, wh: c_int, next: [*c]c_uint) callconv(.c) [*c]c_uint {
    const cursor: *u32 = @ptrFromInt(0x1F800000);
    const packet = c.pc_sprite_packet(@ptrFromInt(@as(usize, cursor.*)), next, brightness, @bitCast(xy), @bitCast(wh), 0);
    cursor.* = psx.addr(@as(?*anyopaque, @ptrFromInt(@intFromPtr(packet) +% 6 * @sizeOf(c_uint))));
    return packet;
}

/// orig: vs_battle_setSpriteDefault (pc/src/battle_helpers.c)
/// A sprite of brightness 0x80 with the default texture page, linked to the
/// default link word (the guest pointer at scratchpad 0x1F800008).
pub fn setSpriteDefault(wh: c_int, xy: c_int) callconv(.c) [*c]c_uint {
    const next: *u32 = @ptrFromInt(0x1F800008);
    return c.vs_battle_setSpriteDefaultTexPage(0x80, xy, wh, @ptrFromInt(@as(usize, next.*)));
}
