//! Actor timer state update: the 0x8E fade timer, the 0x9A timer and the 0x98 countdown
//! (src/BATTLE/BATTLE.PRG/73700.c).
const psx = @import("common").psx;
const v = @import("views_73700");
const c = @import("c_73700");

/// orig: func_800DBFE4 (src/BATTLE/BATTLE.PRG/73700.c)
/// Advances an actor timer state's timers and sends the matching phase codes for its id.
pub fn func_800DBFE4(state: ?*v.actorTimerState) callconv(.c) void {
    const s = state.?;
    if (s.timer8E != 0) {
        s.timer8E +%= 1;
        if (s.timer8E != 0) {
            const t: c_int = s.timer8E;
            const value: c_int = if (t < 8) 0 else if (t < 16) 1 else if (t < 24) 2 else 3;
            if (@as(c_int, s.timer8E) <= 120) {
                c.func_800D820C(psx.conv(c_int, s.id), value +% 1);
                return;
            }
            c.func_800E4C8C(@ptrCast(s));
            c.func_800D82CC(@ptrCast(s));
        }
    }
    if (s.timer9A != 0) {
        s.timer9A -%= 1;
        if (s.timer9A != 0) {
            c.func_800D820C(psx.conv(c_int, s.id), 4);
        } else {
            c.func_800D820C(psx.conv(c_int, s.id), 0);
        }
    }
    if (s.timer98 != 0) {
        s.timer98 -%= 1;
    }
}
