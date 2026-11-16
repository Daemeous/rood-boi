//! CLUT slot transforms for a battle effect (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_render_transformSceneColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Transforms CLUT slots 0, 1, 2 and 13, with func_80047FC0 run between the slot 2 and slot 13 transforms.
pub fn vs_render_transformSceneColors(arg0: c_int, arg1: c_int, arg2: c_short, arg3: c_short, arg4: c_short) callconv(.c) void {
    c.vs_main_transformClutSlot(arg0, arg1, 0, arg2, arg3, arg4);
    c.vs_main_transformClutSlot(arg0, arg1, 1, arg2, arg3, arg4);
    c.vs_main_transformClutSlot(arg0, arg1, 2, arg2, arg3, arg4);
    c.func_80047FC0(arg0, arg1, 0, arg2, arg3, arg4);
    c.vs_main_transformClutSlot(arg0, arg1, 0xD, arg2, arg3, arg4);
}
