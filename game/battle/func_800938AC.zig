//! Calls the room section-14 routine with the current room index, using a
//! 0x20 or 0x7F limit depending on the argument (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800938AC (src/BATTLE/BATTLE.PRG/2842C.c)
/// Runs the room's section-14 routine for the current room, when one is loaded and the room index is set.
pub fn func_800938AC(arg0: c_int) callconv(.c) void {
    const roomIndex: c_int = @as(c_int, v.g.D_800F2258().*);
    if (roomIndex == -1) return;

    const section = psx.get(v.g.vs_battle_roomData().section14) orelse return;
    const sectionArg: [*c]c_int = @ptrCast(section);

    if (arg0 != 0) {
        c.func_80046494(0x200, sectionArg, roomIndex, 0x20, 8);
    } else {
        c.func_80046494(0x200, sectionArg, roomIndex, 0x7F, 8);
    }
}
