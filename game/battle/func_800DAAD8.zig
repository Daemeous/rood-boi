//! Runs the pending target checks of a battle action state, then clears each actor's target mode
//! (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DAAD8 (src/BATTLE/BATTLE.PRG/71618.c)
/// Runs the action's pending target checks and tests every actor's targeting category; returns 1 when one of them stops the action.
pub fn func_800DAAD8(state: ?*v.da4bcState) callconv(.c) c_int {
    const s = state.?;
    const entry = psx.get(s.entries[0]);

    if (s.pending == 0 and c.func_800DA4BC(@ptrCast(s), @ptrCast(entry)) != 0)
        return 1;
    const target: c_int = psx.getBits(s, "target16");
    if (target < 16) {
        if (c.func_800DB370(@ptrCast(s), target, 384, 0) != 0) {
            if (s.mode == 1 and s.action != 7) {
                const et: *v.da4bcEntryTarget = @ptrCast(&entry.?.flagsC);
                s.target15C = psx.getBits(et, "target");
                c.func_800E678C(@ptrCast(s));
                c.func_800E2CCC(@ptrCast(s));
            }
            return 1;
        }
        if (c.func_800DB74C(@ptrCast(s), target) != 0)
            return 1;
    }

    const ts: *v.da4bcTargetState = @ptrCast(s);
    var actor: ?*v.da4bcActor = @ptrCast(v.g.vs_battle_actors()[0]);
    while (actor) |a| : (actor = psx.get(a.next)) {
        if (psx.getBits(psx.at(&ts.targets, a.id), "mode") != 3 and
            psx.getBits(psx.at(&ts.targets, a.id), "category") != 0)
        {
            psx.setBits(psx.at(&ts.targets, a.id), "mode", 3);
            if (c.func_800DB61C(@ptrCast(s), 0) != 0) {
                psx.setBits(psx.at(&ts.targets, a.id), "mode", psx.getBits(psx.at(&ts.targets, a.id), "oldMode"));
                return 1;
            }
            psx.setBits(psx.at(&ts.targets, a.id), "mode", psx.getBits(psx.at(&ts.targets, a.id), "oldMode"));
        }
    }
    return 0;
}
