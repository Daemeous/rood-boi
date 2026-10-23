//! Advances the 7 flag in each entry of the table at *D_800F1BAC to 6, for D_800F227E entries
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_800971D4 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Changes every table entry whose byte 8 is 7 to 6, over the count in D_800F227E.
pub fn func_800971D4() callconv(.c) void {
    // The table pointer is read once, before the loop, as the C's `p` is.
    const first = v.g.D_800F1BAC().*;
    var i: c_int = 0;
    // The count is re-read each iteration, as the C's loop condition does.
    while (i < v.g.D_800F227E().*) : (i += 1) {
        const base: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(first.?));
        const p = psx.at(base, i);
        if (p.unk8.u8[0] == 7) {
            p.unk8.u8[0] = 6;
        }
    }
}
