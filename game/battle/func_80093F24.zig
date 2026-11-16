//! Clears a stored entry in the 2842C table and appends a new one for `arg0` (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093F24 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Releases the entry of type 5 if one is stored, then, for a nonzero `arg0`, appends a type-5 entry holding it (capped at 999).
pub fn func_80093F24(arg0: c_int) callconv(.c) c_int {
    const base_raw: ?*align(2) anyopaque = @alignCast(v.g.vs_battle_floatingLabels().*);
    const base: [*c]v.D_800F1BAC_t = @ptrCast(base_raw);

    var i: c_int = 0;
    while (i < @as(c_int, v.g.vs_battle_floatingLabelCount().*)) : (i +%= 1) {
        if (psx.at(base, i).*.unk8.u16 == 5) {
            c.vs_battle_removeFloatingLabel(i);
            break;
        }
    }

    if (arg0 == 0) {
        return 0;
    }

    const q_raw = c.vs_battle_reserveFloatingLabel();
    const q: [*c]v.D_800F1BAC_t = @ptrCast(@alignCast(q_raw));
    q.*.unk8.u8[0] = 5;
    q.*.unkB = 0;
    q.*.unk8.u8[1] = 0;
    q.*.unkC = psx.conv(u16, arg0);
    if (@as(c_int, q.*.unkC) >= 1000) {
        q.*.unkC = 999;
    }
    v.g.vs_battle_floatingLabelCount().* = psx.conv(i16, @as(c_int, v.g.vs_battle_floatingLabelCount().*) +% 1);
    return 0;
}
