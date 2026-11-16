//! Battle helper that forwards a vector's X and Z to a routine (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007CFF8 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs vs_field_setActorCollisionBypass(arg0, 2), then vs_battle_aiSetActorNavigationOverride(arg0, vec.vx, vec.vz).
pub fn func_8007CFF8(arg0: c_int, vec: ?*v.SVECTOR) callconv(.c) void {
    c.vs_field_setActorCollisionBypass(arg0, 2);
    const p = vec.?;
    c.vs_battle_aiSetActorNavigationOverride(arg0, @as(c_int, p.vx), @as(c_int, p.vz));
}
