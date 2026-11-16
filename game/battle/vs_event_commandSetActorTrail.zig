//! Battle script helper: looks up a short from the operand and calls one of two host functions by its flag byte (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

extern fn vs_event_resolveScriptActorReference(c_int) c_int;

/// orig: vs_event_commandSetActorTrail (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Maps the short at operand+2 through vs_event_resolveScriptActorReference, then calls func_8006C480 or func_8006C4A4 with it and operand byte 4.
pub fn vs_event_commandSetActorTrail(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp_v0: c_short = psx.conv(c_short, vs_event_resolveScriptActorReference(@as(c_int, c.vs_battle_getShort(arg0 + 2))));

    if (arg0[1] != 0) {
        c.func_8006C480(@as(c_int, temp_v0), @as(c_int, arg0[4]));
    } else {
        c.func_8006C4A4(@as(c_int, temp_v0), @as(c_int, arg0[4]));
    }
    return 0;
}
