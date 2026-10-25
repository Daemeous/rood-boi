//! Resets a battle entity slot (D_800F4538) to its spawn state (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009D468 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Resets the entity in slot `index` of D_800F4538 to its spawn state, if the slot is in use.
pub fn func_8009D468(index: c_int, _: c_int) callconv(.c) void {
    if (psx.get(psx.at(v.g.D_800F4538(), index).*)) |slot| {
        slot.unk0.unk2C = 0x1000;
        slot.unk0.unk2E = 0x1000;
        slot.unk0.unk30 = 0x1000;
        slot.unk17FC = -2;
        slot.unk0.unk11 = 0;
        slot.unk5C8 = 0;
        slot.unk0.unk18 = 0;
        @as(*align(1) i32, @ptrCast(&slot.unk0.pitch)).* = 0; // *(int*)&unk24: pitch and facing
        @as(*align(1) i32, @ptrCast(&slot.unk0.roll)).* = 0; // *(int*)&unk28: roll and unk2A
        slot.unk0.unk1A = 0;
        slot.unk0.unk16 = 0;
        slot.unk0.unk14 = 0;
        psx.setBits(&slot.unk0, "unkA_0", 0);
        psx.setBits(slot, "unk5AC_0", 0);
        psx.setBits(&slot.unk0, "unkB_0", 0);
        psx.setBits(&slot.unk0, "unkB_4", 0);
        _ = c.func_8009FD38(@ptrCast(slot));
        if (v.g.D_800F2450()[v.g.D_800E8F2C().*] != 0) {
            _ = c.func_800AB098(@ptrCast(slot), 0, 0);
            _ = c.func_800AB098(@ptrCast(slot), 1, 0);
        }
        slot.unk1802[0] = 0;
        psx.setBits(&slot.unk180C[0], "unk0_0", 0);
        psx.setBits(slot, "unk5AC_21", 0);
        psx.setBits(&slot.unk180C[1], "unk0_0", 0);
        psx.setBits(slot, "unk5AC_22", 0);
        _ = c.func_800A07FC(index, null);
        if (slot.unk17FD == 1) {
            slot.unk17FD = 0;
        }
        psx.setBits(&slot.unk0, "unk9_7", 1);
        var sp10: v.D_800F4538_unk64 = undefined;
        sp10.unk0 = 0;
        _ = c.func_8009F990(index, psx.cptr(c.D_800F4538_unk64, &sp10));
        slot.unk5BE = 4;
        slot.unk17FF = 0;
        slot.unk0.unk64.unk0 = 0;
        slot.unk1860 = 0;
        psx.setBits(&slot.unk0, "unkC_0", 0);
        psx.setBits(&slot.unk0, "unkC_4", 0);
        psx.setBits(&slot.unk0, "unkA_3", 0);
        psx.setBits(&slot.unk0, "unk9_6", 0);
        psx.setBits(slot, "unk5AC_3", 0);
        psx.setBits(&slot.unk0, "unk9_0", 0);
        var var_a2: c_int = 0x62;
        if (index == 0) {
            var_a2 = 1;
        }
        _ = c.func_800A0204(index, var_a2, 0, 0);
        slot.unk6E0 = 0;
        slot.unk6EC = 0;
        slot.unk6EE = 0;
        slot.unk6EF = 0;
        slot.unk1846 = 0;
        psx.setBits(slot, "unk5AC_24", 0);
        psx.setBits(&slot.unk0, "skip", 1);
        psx.setBits(&slot.unk0, "visible", 1);
        psx.setBits(&slot.unk0, "freeze", 1);
        psx.setBits(slot, "unk5AC_15", 0);
        const s0 = &slot.unk1864;
        _ = c.memset(s0, @as(c_int, 0), @as(usize, @sizeOf(v.D_800F4538_unk1864)));
        s0.unkC = slot.shadowRadiusExpandRate;
        s0.unkE = slot.shadowRadiusShrinkRate;
        _ = c.memset(&slot.unk1848, @as(c_int, 0), @as(usize, @sizeOf(v.D_800F4538_unk1848)));
    }
}
