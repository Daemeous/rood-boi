//! An effect-script opcode: stores a script halfword into a register byte (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4AEC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// unk10[next byte] = (u8) next halfword; returns 1.
pub fn func_800D4AEC(script: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const s = script.?;
    // Object order: the value (func_800D5198) before the index (func_800D5170).
    const value = c.func_800D5198(@ptrCast(s));
    const index = c.func_800D5170(@ptrCast(s));
    psx.at(&s.unk10, index).* = psx.conv(u8, value); // the byte index can pass unk10's 4 (the C does not check)
    return 1;
}
