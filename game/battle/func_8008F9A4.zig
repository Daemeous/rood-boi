//! Sets the entry count and flag bytes, then calls func_8008FA14 on each of the first arg0 entries from vs_field_weatherParticles (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008F9A4 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores arg1 and arg0 into the battle globals, then calls func_8008FA14 on each of arg0 entries from vs_field_weatherParticles.
pub fn func_8008F9A4(arg0: c_int, arg1: c_int) callconv(.c) void {
    // The entry pointer is walked in 16-byte steps from the value of vs_field_weatherParticles, NULL included, as the C adds to it.
    var entry: usize = @intFromPtr(v.g.vs_field_weatherParticles().*);
    v.g.vs_field_roomWeatherKind().* = psx.conv(u8, arg1);
    v.g.vs_field_weatherParticleCapacity().* = psx.conv(i16, arg0);
    v.g.vs_field_weatherParticleCount().* = psx.conv(i16, arg0);

    var i: c_int = 0;
    while (i < arg0) : (i +%= 1) {
        c.func_8008FA14(@ptrFromInt(entry));
        entry +%= @sizeOf(v.D_800F1DD4_t);
    }
}
