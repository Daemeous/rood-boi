//! Sets the battle global vs_field_isFieldRenderingEnabled from a flag (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_setFieldRenderingEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores 1 in vs_field_isFieldRenderingEnabled when arg0 is nonzero, else 0.
pub fn vs_field_setFieldRenderingEnabled(arg0: c_int) callconv(.c) void {
    v.g.vs_field_isFieldRenderingEnabled().* = if (arg0 != 0) 1 else 0;
}
