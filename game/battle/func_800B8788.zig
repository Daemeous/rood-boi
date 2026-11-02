//! An event script opcode that sets or clears a pair of an actor's four-byte settings (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B8788 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8009F940 for the actor of getShort(script + 1) with flag 1, script[3..6] and
/// script[7..9], or with flag 0 and no pointers when script[7..9] are all 0.
pub fn func_800B8788(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var entries: [6]v.D_800F4538_unk58_2 = undefined;
    entries[0].unk0 = script[7];
    entries[0].unk1 = script[8];
    entries[0].unk2 = script[9];
    // entries[0].unk3 is uninitialised in the original and func_8009F940 copies all four bytes
    // into the actor (unk6F8.unk3): 0 here (PORTING.md §11.4).
    entries[0].unk3 = 0;
    if (entries[0].unk0 | entries[0].unk1 | entries[0].unk2 != 0) {
        entries[2].unk0 = script[3];
        entries[2].unk1 = script[4];
        entries[2].unk2 = script[5];
        entries[2].unk3 = script[6];
        c.func_8009F940(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))), 1, psx.cptr(c.D_800F4538_unk58_2, &entries[2]), psx.cptr(c.D_800F4538_unk58_2, &entries[0]));
    } else {
        c.func_8009F940(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))), 0, null, null);
    }
    return 0;
}
