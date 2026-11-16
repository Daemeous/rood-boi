//! Battle CLUT transform for slots below 9, then the follow-up helper (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_render_transformActorColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Transforms CLUT slot arg2 + 4, then runs func_80047FC0 for arg2 + 1, when arg2 is below 9.
pub fn vs_render_transformActorColors(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_short, arg4: c_short, arg5: c_short) callconv(.c) void {
    if (arg2 < 9) {
        c.vs_main_transformClutSlot(arg0, arg1, arg2 +% 4, arg3, @as(c_int, arg4), @as(c_int, arg5));
        c.func_80047FC0(arg0, arg1, arg2 +% 1, @as(c_int, arg3), @as(c_int, arg4), @as(c_int, arg5));
    }
}
