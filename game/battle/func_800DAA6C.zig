//! One step of a da4b state: runs one of two helpers by its flagE, then returns 1 (src/BATTLE/BATTLE.PRG/71618.c).
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DAA6C (src/BATTLE/BATTLE.PRG/71618.c)
/// Runs one step of a da4b state (flagE selects the helper) and always returns 1.
pub fn func_800DAA6C(state: ?*v.da4bcState) callconv(.c) c_int {
    const s = state.?;
    if (s.flagE != 0) {
        if (c.func_800DB61C(@ptrCast(s), 1) == 0)
            c.func_800DB5B0(s);
        return 1;
    }
    if (c.func_800DA1D4(@ptrCast(s)) == 0)
        c.func_800DB5FC(s);
    return 1;
}
