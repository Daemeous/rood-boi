//! Steers a movement destination toward the angle, or back to the opposite side if that is closer
//! (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: vs_battle_aiMoveAwayFromReference (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Aims a movement destination along `angle`, keeping the opposite-side candidate when it is nearer the reference point.
pub fn vs_battle_aiMoveAwayFromReference(state: ?*v.vs_battle_aiState, angle: c_int) callconv(.c) void {
    const s = state.?;
    const opposite: c_int = (angle +% 0x800) & 0xFFF;

    c.vs_battle_aiProjectClearRadialDestination(@ptrCast(s), s.tile.position.raw, angle, psx.conv(c_uint, s.distance));
    const previous: c_uint = s.destination168.movementPosition.raw;
    c.vs_battle_aiProjectClearRadialDestination(@ptrCast(s), s.tile.position.raw, opposite, psx.conv(c_uint, s.distance));

    const distance: c_uint = c.vs_core_mathSquaredVectorLength(
        @as(c_int, psx.getBits(&s.destination168.movementPosition.p, "x")) *% 128 -% s.moveX +% 64,
        0,
        @as(c_int, psx.getBits(&s.destination168.movementPosition.p, "z")) *% 128 -% s.moveZ +% 64,
    );

    // previous's bytes 0 and 2 are its x and z fields (little-endian, as the C reads them).
    const bytes: [4]u8 = @bitCast(previous);
    const other: c_uint = c.vs_core_mathSquaredVectorLength(
        @as(c_int, bytes[0]) *% 128 -% s.moveX +% 64,
        0,
        @as(c_int, bytes[2]) *% 128 -% s.moveZ +% 64,
    );
    if (distance < other) {
        s.destination168.movementPosition.raw = previous;
    }

    c.func_800E153C(@ptrCast(s), angle);
}
