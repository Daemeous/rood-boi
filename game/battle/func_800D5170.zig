//! The next byte of an effect script (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800D5170 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the script byte at the cursor unkA and advances the cursor (a u16: it wraps).
pub fn func_800D5170(script: ?*v.D_800F53B8_t) callconv(.c) u8 {
    const s = script.?;
    const cursor = s.unkA;
    s.unkA = cursor +% 1;
    return @as([*]u8, @ptrCast(psx.get(s.unkC).?))[cursor];
}
