//! Battle actor flags, weapon-model render bits and the paired actor's flags, set from one flag word (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_800A087C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets an actor's flags and weapon-model render bits from the bits of `arg1`, then does the same for its paired actor.
pub fn func_800A087C(actorId: c_int, arg1: c_int) callconv(.c) void {
    const temp_t0 = psx.at(v.g.D_800F4538(), actorId).*;

    if (temp_t0 == null) {
        const temp_a0 = psx.at(v.g.D_800F45E0(), actorId).*;
        if (temp_a0) |a| {
            psx.setBits(a, "unk8_0", psx.conv(u1, (arg1 & 1) ^ 1));
            psx.setBits(a, "unk8_1", psx.conv(u1, arg1 >> 1));
        }
    } else {
        const t = temp_t0.?;

        psx.setBits(&t.unk0, "skip", psx.conv(u1, (arg1 & 1) ^ 1));
        psx.setBits(&t.unk0, "visible", psx.conv(u1, arg1 >> 1));
        psx.setBits(&t.unk0, "freeze", psx.conv(u1, arg1 >> 2));
        psx.setBits(&t.unk0, "unk9_7", psx.conv(u1, arg1 >> 6));

        const model0 = psx.at(v.g.vs_battle_wepModels(), actorId *% 2).*;
        const new_var0: c_int = (arg1 >> 11) & 1;
        if (model0) |m| {
            psx.setBits(m, "unk8_4", psx.conv(u1, new_var0));
        }

        const model1 = psx.at(v.g.vs_battle_wepModels(), actorId *% 2 +% 1).*;
        const new_var1: c_int = (arg1 >> 12) & 1;
        if (model1) |m| {
            psx.setBits(m, "unk8_4", psx.conv(u1, new_var1));
        }

        if (@as(c_int, t.unk17FD) >= 2) {
            c.func_800A087C(@as(c_int, t.unk17FD), arg1);
        }
    }
}
