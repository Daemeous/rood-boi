//! Battle script opcode: dispatches a sub-command on the low bits of byte 3
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: func_800B860C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the battle script sub-command selected by bits 2-3 of `arg0[3]`, returning 0.
pub fn func_800B860C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const b3: c_int = @as(c_int, arg0[3]);
    switch ((b3 >> 2) & 3) {
        0 => {
            c.func_8007CB84(b3 & 3, @as(c_int, c.vs_battle_getShort(psx.at(arg0, 1))));
        },
        1 => {
            c.func_8007CBDC(b3 & 3, @as(c_int, c.vs_battle_getShort(psx.at(arg0, 1))), (b3 >> 4) +% 0x1A);
        },
        2 => {
            c.vs_battle_loadEtm(b3 & 3, @as(c_int, c.vs_battle_getShort(psx.at(arg0, 1))), (b3 >> 4) +% 0x1A);
        },
        else => {},
    }
    return 0;
}
