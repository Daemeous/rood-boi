//! Moves a battle actor by its velocity, updates its current tile and calls
//! func_800AA850 (src/BATTLE/BATTLE.PRG/38C1C.c).
const psx = @import("common").psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A291C (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Adds the velocity to the position, sets the current tile from it and calls func_800AA850.
pub fn func_800A291C(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const self = arg0.?;
    const pos = &self.unk0.position;
    const vel = &self.unk1848.unk10;

    // addVector: three i16 additions, each truncated back to i16 as the C does.
    pos.vx = psx.conv(i16, @as(c_int, pos.vx) +% @as(c_int, vel.vx));
    pos.vy = psx.conv(i16, @as(c_int, pos.vy) +% @as(c_int, vel.vy));
    pos.vz = psx.conv(i16, @as(c_int, pos.vz) +% @as(c_int, vel.vz));

    // Signed division truncates toward zero, as C's does.
    self.unk0.currentTileX = psx.conv(u8, @divTrunc(@as(c_int, pos.vx), 128));
    self.unk0.currentTileZ = psx.conv(u8, @divTrunc(@as(c_int, pos.vz), 128));

    c.func_800AA850(self.unk0.unkF, self.unk1848.unk6, 12);
}
