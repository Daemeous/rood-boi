//! Battle: the per-entry geometry fixup over the table D_800F1DD8 (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80093788 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Flips entry arg0 of D_800F1DD8 once: its geometry vertex flag and its record's sign and fields.
pub fn func_80093788(arg0: c_int) callconv(.c) void {
    if (arg0 < 0x40) {
        // Signed index: the C does not check arg0 < 0 either.
        const entry = psx.at(v.g.D_800F1DD8(), arg0);
        if (entry.unk1 == 6) {
            const temp_a2 = psx.get(entry.unk4);
            const a0 = psx.get(v.g.vs_battle_roomData().geometrySection);
            if (entry.unk0 != 0) {
                const a2 = temp_a2.?;
                const geo = a0.?;
                // a0 is a byte base (void*): temp_v1 = a0 + unk0, then word indices 5 and 0.
                const off: isize = @as(isize, a2.unk0);
                psx.byteAt(c_int, geo, off + 20).* = a2.unk18;
                psx.byteAt(c_int, geo, off).* |= 0x6000;
                const temp_v0: c_int = -%a2.unk20;
                const temp_a1: c_int = a2.unk1C;
                a2.unk20 = temp_v0;
                a2.unk8 = temp_v0;
                a2.unk1C = a2.unk18;
                a2.unk18 = temp_a1;
                a2.unkC = -%a2.unkC;
                entry.unk0 = 0;
            }
        }
    }
}
