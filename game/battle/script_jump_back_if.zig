//! Script opcode: jumps back by the script's short offset while a counter is
//! nonzero (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
const event_jump = @import("event_jump.zig");

/// orig: vs_battle_script_jumpBackIf (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Jumps back by the short offset at arg0[2] when counter arg0[1] is 0xFF, or is nonzero after its decrement; else returns 0.
pub fn script_jumpBackIf(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const take_back = if (arg0[1] == 0xFF) true else blk: {
        const counter = &v.g.vs_event_loopCounters()[arg0[1]];
        counter.* -%= 1;
        break :blk counter.* != 0;
    };
    if (!take_back) return 0;
    const off: c_int = -%@as(c_int, c.vs_battle_getShort(psx.at(arg0, 2)));
    return event_jump.to(psx.at(arg0, off));
}
