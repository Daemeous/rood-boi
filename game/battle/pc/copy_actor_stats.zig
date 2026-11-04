//! Copying an actor's stat words and block (D2: 73B0C.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const c = @import("c_battle_helpers");

/// orig: func_800DC3E8 (pc/src/battle_helpers.c)
/// Copies the stat words at 0x18, 0x1C, 0x948 and 0x30 (in that order), then
/// the 0x528-byte block at 0x398, from `src` to `dst`.
pub fn copyActorStats(src: [*]u8, dst: [*]u8) callconv(.c) void {
    psx.byteAt(c_uint, dst, 0x18).* = psx.byteAt(c_uint, src, 0x18).*;
    psx.byteAt(c_uint, dst, 0x1c).* = psx.byteAt(c_uint, src, 0x1c).*;
    psx.byteAt(c_uint, dst, 0x948).* = psx.byteAt(c_uint, src, 0x948).*;
    psx.byteAt(c_uint, dst, 0x30).* = psx.byteAt(c_uint, src, 0x30).*;
    c.vs_main_memcpy(psx.byteAt(u8, dst, 0x398), psx.byteAt(u8, src, 0x398), 0x528);
}
