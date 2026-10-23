//! Clears a stored entry in the 2842C table and appends a new one for `arg0` (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093F24 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Releases the entry of type 5 if one is stored, then, for a nonzero `arg0`, appends a type-5 entry holding it (capped at 999).
pub fn func_80093F24(arg0: c_int) callconv(.c) c_int {
    const base_raw: ?*align(2) anyopaque = @alignCast(v.g.D_800F1BAC().*);
    const base: [*c]v.D_800F1BAC_t = @ptrCast(base_raw);

    var i: c_int = 0;
    while (i < @as(c_int, v.g.D_800F227E().*)) : (i +%= 1) {
        if (psx.at(base, i).*.unk8.u16 == 5) {
            c.func_80096444(i);
            break;
        }
    }

    if (arg0 == 0) {
        return 0;
    }

    const q_raw = c.func_800962E4();
    const q: [*c]v.D_800F1BAC_t = @ptrCast(@alignCast(q_raw));
    q.*.unk8.u8[0] = 5;
    q.*.unkB = 0;
    q.*.unk8.u8[1] = 0;
    q.*.unkC = psx.conv(u16, arg0);
    if (@as(c_int, q.*.unkC) >= 1000) {
        q.*.unkC = 999;
    }
    v.g.D_800F227E().* = psx.conv(i16, @as(c_int, v.g.D_800F227E().*) +% 1);
    return 0;
}
