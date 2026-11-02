//! An event script opcode that sets or clears an actor's three-byte setting (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B81A0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800A07FC for the actor of getShort(script + 1) with script[3..5] as a
/// D_800F4538_unk58_2, or with no pointer when all three are 0.
pub fn func_800B81A0(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var setting: v.D_800F4538_unk58_2 = undefined;
    setting.unk0 = script[3];
    setting.unk1 = script[4];
    setting.unk2 = script[5];
    // unk3 is uninitialised in the original and func_800A07FC copies all four bytes into the
    // actor (unk0.unk58.unk3): 0 here (PORTING.md §11.4).
    setting.unk3 = 0;
    if (setting.unk0 | setting.unk1 | setting.unk2 != 0) {
        _ = c.func_800A07FC(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))), psx.cptr(c.D_800F4538_unk58_2, &setting));
    } else {
        _ = c.func_800A07FC(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))), null);
    }
    return 0;
}
