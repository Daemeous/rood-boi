//! Steers a movement destination toward the angle, or back to the opposite side if that is closer
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E1764 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Aims a movement destination along `angle`, keeping the opposite-side candidate when it is nearer the reference point.
pub fn func_800E1764(state: ?*v.movementDestinationState, angle: c_int) callconv(.c) void {
    const s = state.?;
    const opposite: c_int = (angle +% 0x800) & 0xFFF;

    c.func_800E1388(@ptrCast(s), s.position.raw, angle, psx.conv(c_uint, s.distance));
    const previous: c_uint = s.destination.raw;
    c.func_800E1388(@ptrCast(s), s.position.raw, opposite, psx.conv(c_uint, s.distance));

    const distance: c_uint = c.func_800E4660(
        @as(c_int, psx.getBits(&s.destination.p, "x")) *% 128 -% s.referenceX +% 64,
        0,
        @as(c_int, psx.getBits(&s.destination.p, "z")) *% 128 -% s.referenceZ +% 64,
    );

    // previous's bytes 0 and 2 are its x and z fields (little-endian, as the C reads them).
    const bytes: [4]u8 = @bitCast(previous);
    const other: c_uint = c.func_800E4660(
        @as(c_int, bytes[0]) *% 128 -% s.referenceX +% 64,
        0,
        @as(c_int, bytes[2]) *% 128 -% s.referenceZ +% 64,
    );
    if (distance < other) {
        s.destination.raw = previous;
    }

    c.func_800E153C(@ptrCast(s), angle);
}
