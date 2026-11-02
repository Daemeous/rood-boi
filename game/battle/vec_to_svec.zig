//! VECTOR to SVECTOR (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_vecToSvec (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Copies x, y, z, truncated to 16 bits.
pub fn vecToSvec(from: ?*v.VECTOR, to: ?*v.SVECTOR) callconv(.c) void {
    to.?.vx = psx.conv(i16, from.?.vx);
    to.?.vy = psx.conv(i16, from.?.vy);
    to.?.vz = psx.conv(i16, from.?.vz);
}
