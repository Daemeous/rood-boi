//! Resets the 146C scratch table of `arg0` entries and stores `arg1` (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008F30C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears the flag byte, stores the two counts and randomises each of the `arg0` table entries.
pub fn func_8008F30C(arg0: c_int, arg1: c_int) callconv(.c) void {
    var s1: ?*v.D_800F1DD4_t = v.g.vs_field_weatherParticles().*;
    v.g.D_800F1DC7().* = 0;
    v.g.vs_field_roomWeatherKind().* = psx.conv(u8, arg1);
    v.g.vs_field_weatherParticleCapacity().* = psx.conv(i16, arg0);
    v.g.vs_field_weatherParticleCount().* = psx.conv(i16, arg0);

    var i: c_int = 0;
    while (i < arg0) : (i +%= 1) {
        const e: *v.D_800F1DD4_t = s1.?;
        e.unkF = psx.conv(u8, c.rand() & 0x7F);
        e.unk0 = psx.conv(i16, @rem(c.rand(), 320));
        e.unk2 = psx.conv(i16, @rem(c.rand(), 240));
        e.unk4 = psx.conv(i16, @rem(c.rand(), 256));
        e.unk8 = -1;
        e.unkA = -1;
        s1 = psx.at(e, 1);
    }
}
