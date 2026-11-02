//! The event script's state-flag arithmetic with an immediate (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_setFlagImm (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Applies script[1]'s operator (+ - * / % & |, or a plain store) to a state flag and the byte script[3].
pub fn script_setFlagImm(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const op: c_int = script[1];
    const imm: i16 = script[3];
    const flag: c_int = (@as(c_int, script[2]) >> 2) +% ((op & 0xF) << 6);
    const id = psx.conv(i16, flag);
    switch (op & 0xF0) {
        0x00 => c.vs_battle_setStateFlag(id, psx.conv(u8, @as(c_int, c.vs_battle_getStateFlag(id)) +% imm)),
        0x10 => c.vs_battle_setStateFlag(id, psx.conv(u8, @as(c_int, c.vs_battle_getStateFlag(id)) -% imm)),
        0x20 => c.vs_battle_setStateFlag(id, psx.conv(u8, @as(c_int, c.vs_battle_getStateFlag(id)) *% imm)),
        0x30 => c.vs_battle_setStateFlag(id, psx.conv(u8, @divTrunc(@as(c_int, c.vs_battle_getStateFlag(id)), imm))),
        0x40 => c.vs_battle_setStateFlag(id, psx.conv(u8, @rem(@as(c_int, c.vs_battle_getStateFlag(id)), imm))),
        0x50 => c.vs_battle_setStateFlag(id, psx.conv(u8, @as(c_int, c.vs_battle_getStateFlag(id)) & imm)),
        0x60 => c.vs_battle_setStateFlag(id, psx.conv(u8, @as(c_int, c.vs_battle_getStateFlag(id)) | imm)),
        0x70 => c.vs_battle_setStateFlag(id, psx.conv(u8, imm)),
        else => {},
    }
    return 0;
}
