//! Battle candidate check for a scene entry: runs the candidate test and, when it
//! passes, the two follow-up steps (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DA4BC (src/BATTLE/BATTLE.PRG/71618.c)
/// Runs the candidate test for an entry and, if it passes, the follow-up steps; returns 1 when one of them succeeds.
pub fn func_800DA4BC(state: ?*v.da4bcState, entry: ?*v.da4bcEntry) callconv(.c) c_int {
    const result: c_int = 0;
    if (c.func_800DB8AC(@ptrCast(state), @ptrCast(entry)) != 0) {
        const e = entry.?;
        const target: c_int = psx.conv(c_int, e.flagsC & 15);
        const distance: c_int = @bitCast(c.vs_gte_rsqrt(@bitCast(e.distance)));
        const s = state.?;
        var radius: c_int = distance +% (@as(c_int, s.radius) << 5);
        radius = radius *% radius;
        v.g.D_800F58BC().*.?.unk24 +%= 1;
        if (s.distances[@intCast(target)] < psx.conv(c_uint, radius)) {
            if ((s.targets[@intCast(e.flagsC & 15)] & 1) != 0) {
                c.func_800D836C(@ptrCast(state));
            }
        }
        if (c.func_800DB370(@ptrCast(state), target, distance, 1) != 0) {
            return 1;
        }
        if (c.func_800DB74C(@ptrCast(state), target) != 0) {
            return 1;
        }
    }
    return result;
}
