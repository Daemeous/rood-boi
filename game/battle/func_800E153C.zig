//! Steers a movement destination state toward its target cell, retrying nearby cells after repeated failures (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E153C (src/BATTLE/BATTLE.PRG/771A8.c)
/// Aims a movement destination state at its target cell, retrying nearby cells after repeated failures.
pub fn func_800E153C(state: ?*v.movementDestinationState, _: c_int) callconv(.c) void {
    const st = state.?;
    const distance: c_int = st.distance;

    const reached: bool = search: {
        if ((st.position.raw & 0xFF00FF) == (st.destination.raw & 0xFF00FF)) break :search false;

        const angle: c_int = c.ratan2(
            @as(c_int, st.x) -% @as(c_int, psx.getBits(&st.destination.p, "x")) *% 128 -% 64,
            @as(c_int, st.z) -% @as(c_int, psx.getBits(&st.destination.p, "z")) *% 128 -% 64,
        );

        const dxv: c_int = @as(c_int, psx.getBits(&st.destination.p, "x")) *% 128 -% @as(c_int, st.x) +% 64;
        const dzv: c_int = @as(c_int, psx.getBits(&st.destination.p, "z")) *% 128 -% @as(c_int, st.z) +% 64;
        if (c.func_800E4660(dxv, 0, dzv) > psx.conv(c_uint, distance *% distance)) {
            c.func_800E1388(@ptrCast(st), st.position.raw, angle, psx.conv(c_uint, distance));
        }

        if ((st.flags & 6) != 0 and (st.flagD != 0 or c.func_800E45F4(st.destination.raw, st.position.raw) < 16)) {
            const prev: i32 = @bitCast(st.previous.raw << 8);
            if (prev >= 0) {
                st.destination = st.previous;
            }
            c.func_800E4B2C(@ptrCast(st), angle);
            return;
        }

        st.attempts +%= 1;
        if (@as(c_int, st.attempts) < 6) break :search false;

        var dest = st.destination;
        if (c.func_800D954C(@ptrCast(st), dest.raw) > 0) break :search true;

        var i: c_int = 0;
        while (i < 4) : (i += 1) {
            const off: usize = @as(usize, @intCast(i)) * 2;
            const entry: *v.D_800F16EC_t = @ptrFromInt(0x1F8003EC + off);
            psx.setBits(&dest.p, "x", psx.conv(u8, @as(c_int, psx.getBits(&st.destination.p, "x")) +% @as(c_int, psx.getBits(entry, "dx"))));
            psx.setBits(&dest.p, "z", psx.conv(u8, @as(c_int, psx.getBits(&st.destination.p, "z")) +% @as(c_int, psx.getBits(entry, "dz"))));
            if (c.func_800D954C(@ptrCast(st), dest.raw) > 0) {
                st.destination = dest;
                break :search true;
            }
        }
        break :search false;
    };

    if (reached) {
        st.attempts = 0;
        st.next = st.destination;
        c.func_800DEEFC(@ptrCast(st), 13);
        return;
    }
    c.func_800E2CCC(@ptrCast(st));
}
