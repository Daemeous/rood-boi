//! Steers a movement destination state toward its target cell, retrying nearby cells after repeated failures (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E153C (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Aims a movement destination state at its target cell, retrying nearby cells after repeated failures.
pub fn func_800E153C(state: ?*v.vs_battle_aiState, _: c_int) callconv(.c) void {
    const st = state.?;
    const distance: c_int = st.distance;

    const reached: bool = search: {
        if ((st.tile.position.raw & 0xFF00FF) == (st.destination168.movementPosition.raw & 0xFF00FF)) break :search false;

        const angle: c_int = c.ratan2(
            @as(c_int, st.position.v.vx) -% @as(c_int, psx.getBits(&st.destination168.movementPosition.p, "x")) *% 128 -% 64,
            @as(c_int, st.position.v.vz) -% @as(c_int, psx.getBits(&st.destination168.movementPosition.p, "z")) *% 128 -% 64,
        );

        const dxv: c_int = @as(c_int, psx.getBits(&st.destination168.movementPosition.p, "x")) *% 128 -% @as(c_int, st.position.v.vx) +% 64;
        const dzv: c_int = @as(c_int, psx.getBits(&st.destination168.movementPosition.p, "z")) *% 128 -% @as(c_int, st.position.v.vz) +% 64;
        if (c.vs_core_mathSquaredVectorLength(dxv, 0, dzv) > psx.conv(c_uint, distance *% distance)) {
            c.vs_battle_aiProjectClearRadialDestination(@ptrCast(st), st.tile.position.raw, angle, psx.conv(c_uint, distance));
        }

        if ((st.flags & 6) != 0 and (st.flagsC.b[1] != 0 or c.vs_core_mathSquaredTileDistance(st.destination168.movementPosition.raw, st.tile.position.raw) < 16)) {
            const prev: i32 = @bitCast(st.previous48.movementPosition.raw << 8);
            if (prev >= 0) {
                st.destination168.movementPosition = st.previous48.movementPosition;
            }
            c.vs_battle_aiMoveActorTowardDestinationTile(@ptrCast(st), angle);
            return;
        }

        st.attempts +%= 1;
        if (@as(c_int, st.attempts) < 6) break :search false;

        var dest = st.destination168.movementPosition;
        if (c.vs_battle_aiDestinationOccupancy(@ptrCast(st), dest.raw) > 0) break :search true;

        var i: c_int = 0;
        while (i < 4) : (i += 1) {
            const off: usize = @as(usize, @intCast(i)) * 2;
            const entry: *align(1) v.D_800F16EC_t = &pad.directions(v.D_800F16EC_t)[off];
            psx.setBits(&dest.p, "x", psx.conv(u8, @as(c_int, psx.getBits(&st.destination168.movementPosition.p, "x")) +% @as(c_int, psx.getBits(entry, "dx"))));
            psx.setBits(&dest.p, "z", psx.conv(u8, @as(c_int, psx.getBits(&st.destination168.movementPosition.p, "z")) +% @as(c_int, psx.getBits(entry, "dz"))));
            if (c.vs_battle_aiDestinationOccupancy(@ptrCast(st), dest.raw) > 0) {
                st.destination168.movementPosition = dest;
                break :search true;
            }
        }
        break :search false;
    };

    if (reached) {
        st.attempts = 0;
        st.next.movementPosition = st.destination168.movementPosition;
        c.vs_battle_aiIssueActorCommand(@ptrCast(st), 13);
        return;
    }
    c.vs_battle_aiRecoverMovement(@ptrCast(st));
}
