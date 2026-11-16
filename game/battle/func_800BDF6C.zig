//! Steps an interpolation toward its target by the game's tick speed (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BDF6C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Advances an active interpolation by half the tick speed and writes its eased value to `unk2`.
pub fn func_800BDF6C(arg0: ?*v.func_800BDF6C_unk180_t) callconv(.c) void {
    const a = arg0.?;
    if (a.unk9 != 0) {
        a.unk8 = psx.conv(u8, @as(c_int, a.unk8) +% (v.g.vs_gametime_tickspeed().* >> 1));
        if (@as(c_int, a.unk9) < @as(c_int, a.unk8)) {
            a.unk8 = a.unk9;
        }
        const scratch: *c_int = @ptrFromInt(pad.addr(0x88));
        scratch.* = c.vs_event_evaluateInterpolationCurve(@as(c_int, a.unk1), @as(c_int, a.unk8), @as(c_int, a.unk9));
        a.unk2 = psx.conv(i16, @as(c_int, a.unk4) +% ((@as(c_int, a.unk6) *% scratch.*) >> 12));
        if (a.unk8 == a.unk9) {
            a.unk9 = 0;
        }
        a.unk0 = 1;
    }
}
