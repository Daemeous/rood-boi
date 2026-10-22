//! Aims the destination of a movement from a source position at an angle and distance,
//! walking inwards until the step is clear and clamping to the battle bounds
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E1388 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Sets the state's destination to the nearest clear cell along a heading from the source, clamped to the bounds.
pub fn func_800E1388(state: ?*v.func_800D8400_t, source: v.vs_battle_movementPosition, angle: c_int, distance: c_uint) callconv(.c) void {
    var dest = v.vs_battle_movementPosition{ .raw = 0 };

    const heading: c_int = (0 -% angle +% 0xC00) & 0xFFF;
    const step: c_uint = distance >> 3;
    var remaining: c_uint = distance;

    var i: c_int = 0;
    while (i < 8) : ({
        i += 1;
        remaining -%= step;
    }) {
        const x: c_int = (@as(c_int, @bitCast(remaining *% @as(c_uint, @bitCast(c.rcos(heading))))) >> 12) +% 64;
        const sx: c_int = @as(c_int, psx.getBits(&source.p, "x"));
        psx.setBits(&dest.p, "x", psx.conv(u8, ((sx *% 128) +% x) >> 7));

        const z: c_int = (@as(c_int, @bitCast(remaining *% @as(c_uint, @bitCast(c.rsin(heading))))) >> 12) +% 64;
        const sz: c_int = @as(c_int, psx.getBits(&source.p, "z"));
        psx.setBits(&dest.p, "z", psx.conv(u8, ((sz *% 128) +% z) >> 7));

        if (c.func_800D954C(@ptrCast(state), dest.raw) > 0) break;
    }

    const bounds = v.g.D_800F58BC().*.?;
    if (@as(c_int, psx.getBits(&dest.p, "x")) < @as(c_int, bounds.minX)) {
        psx.setBits(&dest.p, "x", bounds.minX);
    } else if (@as(c_int, psx.getBits(&dest.p, "x")) > @as(c_int, bounds.maxX)) {
        psx.setBits(&dest.p, "x", bounds.maxX);
    }
    if (@as(c_int, psx.getBits(&dest.p, "z")) < @as(c_int, bounds.minZ)) {
        psx.setBits(&dest.p, "z", bounds.minZ);
    } else if (@as(c_int, psx.getBits(&dest.p, "z")) > @as(c_int, bounds.maxZ)) {
        psx.setBits(&dest.p, "z", bounds.maxZ);
    }

    state.?.destination = dest;
}
