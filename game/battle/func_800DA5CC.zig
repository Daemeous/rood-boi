//! Target lookup for the da4bc state: tries the entry's target, then the state's own target
//! (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DA5CC (src/BATTLE/BATTLE.PRG/71618.c)
/// Resolves the state's target, returning 1 when a target was handled and 0 otherwise.
pub fn func_800DA5CC(state: ?*v.da4bcState) callconv(.c) c_int {
    const s = state.?;
    const entry = psx.get(s.entries[0]);

    if (s.pending == 0 and c.func_800DA4BC(@ptrCast(s), @ptrCast(entry)) != 0) return 1;

    const target: c_int = @as(c_int, psx.getBits(s, "target16"));
    if (target < 16) {
        if (c.func_800DB370(@ptrCast(s), target, 288, 1) != 0) {
            if (s.mode == 1 and s.action != 7) {
                // The entry's flagsC target field; a null entry reads through the C's NULL pointer.
                const et: *v.da4bcEntryTarget = @ptrCast(&entry.?.flagsC);
                s.target15C = psx.conv(u8, @as(c_int, psx.getBits(et, "target")));
                c.func_800E678C(@ptrCast(s));
                c.func_800E2CCC(@ptrCast(s));
            }
            return 1;
        }
        if (c.func_800DB74C(@ptrCast(s), target) != 0) return 1;
    }
    return 0;
}
