//! The movement recovery step: gives up after a long wait, else faces the
//! target or the best open direction (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: vs_battle_aiRecoverMovement (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Recovery step of a movement: gives up after a long wait, else faces its target or the best open direction.
pub fn vs_battle_aiRecoverMovement(state: ?*v.vs_battle_aiState) callconv(.c) void {
    const st = state.?;
    const context = psx.get(st.object.movementRecoveryContext).?;

    if ((context.flags & 0x200000) != 0) {
        // state->counter++ is evaluated only when the context flag is set.
        const old = st.counter;
        st.counter +%= 1;
        if (old > 120 and (st.flags & 6) != 0 and (st.flags & 0x30) != 0 and
            c.vs_battle_aiDestinationOccupancy(@ptrCast(st), st.tile.position.raw) > 0)
        {
            c.vs_battle_aiReleaseActorAiReservations(@ptrCast(st));
            c.func_800E4B10(st);
            return;
        }
    }

    if (st.movementTargetId != 16) {
        const actorSlot = psx.at(v.g.vs_actors_actorRecords(), st.movementTargetId).*;
        const actor: *v.movementActorPosition = @ptrCast(actorSlot.?);
        const direction: c_int = c.ratan2(
            @as(c_int, st.position.v.vx) -% @as(c_int, actor.x),
            @as(c_int, st.position.v.vz) -% @as(c_int, actor.z),
        ) & 0xFFF;
        st.angle18A = psx.conv(i16, direction);
        st.timer13A = st.duration;
        c.vs_battle_aiIssueActorCommand(@ptrCast(st), 4);
    } else if (st.flags8.b[1] != 0) {
        const direction: c_int = c.ratan2(
            @as(c_int, st.position.v.vx) -% @as(c_int, st.targetX),
            @as(c_int, st.position.v.vz) -% @as(c_int, st.targetZ),
        ) & 0xFFF;
        st.heading = psx.conv(i16, direction);
        st.timer13A = st.duration;
        c.vs_battle_aiIssueActorCommand(@ptrCast(st), 3);
    } else {
        var bestHeight: c_int = -2147483647 - 1;
        var bestDirection: c_int = -1;
        const source = st.tile.position;
        const x: c_uint = source.raw & 255;
        const z: c_uint = (source.raw >> 16) & 255;
        const angle: c_uint = psx.conv(c_uint, @as(c_int, context.angle26) +% @as(c_int, context.angle14));
        const quadrant: c_uint = (angle & 0xFFF) >> 10;
        var direction: c_int = if ((angle & 0x3FF) > 512)
            psx.conv(c_int, ((quadrant +% 1) & 3) *% 2)
        else
            psx.conv(c_int, quadrant *% 2);

        var i: c_int = 0;
        const table: [*]v.D_800F16EC_t = pad.directions(v.D_800F16EC_t);
        while (i < 4) : (i += 1) {
            direction = (direction +% i *% 2) & 7;
            const entry = &table[@intCast(direction)];
            const dx: c_int = @as(c_int, psx.getBits(entry, "dx"));
            const dz: c_int = @as(c_int, psx.getBits(entry, "dz"));
            const nx: c_int = @as(c_int, psx.conv(u8, x +% psx.conv(c_uint, dx)));
            const nz: c_int = @as(c_int, psx.conv(u8, z +% psx.conv(c_uint, dz)));
            if (nx < @as(c_int, pad.move.width) and nz < @as(c_int, pad.move.depth)) {
                const height: c_int = (c.vs_field_groundHeightFlagsAtTile(nx, nz) << 17) >> 17;
                if (height > bestHeight) {
                    bestDirection = direction;
                    bestHeight = height;
                }
            }
        }

        if (bestDirection < 0) {
            c.vs_battle_aiIssueActorCommand(@ptrCast(st), 0);
        } else {
            st.angle18A = psx.conv(i16, bestDirection << 9);
            st.timer13A = st.duration;
            c.vs_battle_aiIssueActorCommand(@ptrCast(st), 4);
        }
    }
}
