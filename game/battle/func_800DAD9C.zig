//! Dispatches action setup over the state's entries, then over the registered actors (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DAD9C (src/BATTLE/BATTLE.PRG/71618.c)
/// Runs the action setup over each entry, then over the fallback entry once per registered actor; returns 1 when one setup reports a match.
pub fn func_800DAD9C(state: ?*v.actionDispatchState) callconv(.c) c_int {
    const st = state.?;

    var i: c_int = 0;
    while (i < st.count) : (i +%= 1) {
        const entry = psx.get(psx.at(&st.entries, i).*);
        if (c.func_800DAC80(@ptrCast(st), @ptrCast(entry)) != 0) return 1;
    }

    const fallback: *v.actionDispatchEntry = &st.fallback;
    if (((fallback.flags4 >> 5) & 255) != 0) {
        const registry: *v.actionDispatchRegistry = @ptrCast(v.g.D_800F58BC().*.?);
        var j: usize = 0;
        while (j < 4) : (j += 1) {
            const actor = psx.get(registry.registered[j]);
            if (actor) |a| {
                fallback.flagsC = (fallback.flagsC & ~@as(u32, 15)) | psx.conv(u32, a.id & 15);
                const model: *v.actionDispatchModel = @ptrCast(psx.at(v.g.D_800F45E0(), a.id).*.?);
                if (model.id != 0 and model.status == 0) {
                    if (c.func_800DAC80(@ptrCast(st), @ptrCast(fallback)) != 0) return 1;
                }
            }
        }
    }

    return 0;
}
