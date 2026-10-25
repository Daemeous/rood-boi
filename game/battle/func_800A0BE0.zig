//! Builds the actor state word D_800F49E0 for an actor slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A0BE0 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Packs the state flags of actor `actorId` into D_800F49E0 and returns them; a slot with no actor record uses its slot record.
pub fn func_800A0BE0(actorId: c_int) callconv(.c) c_uint {
    const out = v.g.D_800F49E0();
    const temp_a1 = psx.at(v.g.D_800F4538(), actorId).*;

    out.* = 0;

    if (temp_a1) |a| {
        out.* = 0x8000;
        if (psx.getBits(&a.unk0, "skip") == 0) out.* = 0x8001;
        if (psx.getBits(&a.unk0, "visible") != 0) out.* |= 2;
        if (psx.getBits(&a.unk0, "freeze") != 0) out.* |= 4;
        if (psx.getBits(a, "unk5AC_0") != 0) {
            out.* |= 0x18;
        } else {
            if (a.unk0.unk1A != 0) out.* |= 8;
            if (a.unk0.unk18 != 0) out.* |= 0x10;
        }
        if (a.unk5C8 != 0) out.* |= 0x20;
        if (psx.getBits(&a.unk0, "unk9_7") != 0) out.* |= 0x40;
        if (psx.getBits(&a.unkC54, "unk548_16") != 0) out.* |= 0x08000080;
        if (psx.getBits(&a.unkC54, "unk548_17") != 0) out.* |= 0x100;
        if (psx.getBits(a, "unk5AC_4") != 0) out.* |= 0x200;
        if (a.unk0.unk12 != 0xFF) out.* |= 0x400;

        // Weapon model slots: the C indexes past the 20-entry array for large unkF; psx.at keeps that.
        const wepIndex: c_int = @as(c_int, a.unk0.unkF) *% 2;
        const wep0 = psx.at(v.g.vs_battle_wepModels(), wepIndex).*;
        if (wep0 != null and psx.getBits(wep0.?, "unk8_4") != 0) out.* |= 0x800;
        const wep1 = psx.at(v.g.vs_battle_wepModels(), wepIndex +% 1).*;
        if (wep1 != null and psx.getBits(wep1.?, "unk8_4") != 0) out.* |= 0x1000;

        // Guest word at &unk5B4 - 2 ints, i.e. byte offset 0x5AC.
        const word: c_uint = psx.byteAt(c_uint, a, 0x5AC).*;
        if ((word & 0x0100_0000) != 0) out.* |= 0x2000;

        if (psx.getBits(&a.unk0, "unkA_0") != 0) out.* |= 0x4000;
        const c0 = psx.getBits(&a.unk0, "unkC_0");
        if (c0 != 0) out.* |= @as(c_uint, c0) << 16;
        if (psx.getBits(a, "unk5AC_3") != 0) out.* |= 0x80000;

        const anim: c_int = @as(c_int, a.animationId) -% 0x47;
        if (psx.conv(c_uint, anim) < 0xE) out.* |= 0x80000;

        if (psx.getBits(&a.unk0, "unkA_3") != 0) out.* |= 0x100000;
        if (psx.getBits(&a.unk0, "unkA_5") != 0) out.* |= 0x200000;
        if ((word & 0xF000_0000) != 0) out.* |= 0x400000;
        if ((word & 0x0001_0000) != 0) out.* |= 0x0800_0000;

        const bits9_0 = psx.getBits(&a.unk0, "unk9_0");
        if (bits9_0 != 0 and bits9_0 < 6) out.* |= 0x0100_0000;

        const ub0 = psx.getBits(&a.unk0, "unkB_0");
        if (ub0 != 0 and ub0 < 3) out.* |= 0x0200_0000;

        if (psx.getBits(&a.unk0, "unkB_4") != 0) out.* |= 0x0400_0000;
        if (psx.getBits(a, "unk5AC_9") != 0) out.* |= 0x0800_0000;
        if (ub0 >= 3) out.* |= 0x1000_0000;
        if (psx.getBits(&a.unk0, "unkA_3") == 1) out.* |= 0x2000_0000;
        if (psx.getBits(&a.unk0, "unk9_6") != 0) out.* |= 0x4000_0000;
    } else {
        const temp_v1 = psx.at(v.g.D_800F45E0(), actorId).*;
        if (temp_v1) |p| {
            if (psx.getBits(p, "unk8_0") == 0) out.* = 1;
            if (psx.getBits(p, "unk8_1") != 0) out.* |= 2;
        }
    }

    return out.*;
}
