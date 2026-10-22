//! The movement recovery step: gives up after a long wait, else faces the
//! target or the best open direction (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E2CCC (src/BATTLE/BATTLE.PRG/771A8.c)
/// Recovery step of a movement: gives up after a long wait, else faces its target or the best open direction.
pub fn func_800E2CCC(state: ?*v.movementRecoveryState) callconv(.c) void {
    const st = state.?;
    const context = psx.get(st.context).?;

    if ((context.flags & 0x200000) != 0) {
        // state->counter++ is evaluated only when the context flag is set.
        const old = st.counter;
        st.counter +%= 1;
        if (old > 120 and (st.flags & 6) != 0 and (st.flags & 0x30) != 0 and
            c.func_800D954C(@ptrCast(st), st.position.raw) > 0)
        {
            c.func_800E678C(@ptrCast(st));
            c.func_800E4B10(st);
            return;
        }
    }

    if (st.targetActor != 16) {
        const actorSlot = psx.at(v.g.D_800F4538(), st.targetActor).*;
        const actor: *v.movementActorPosition = @ptrCast(actorSlot.?);
        const direction: c_int = c.ratan2(
            @as(c_int, st.x) -% @as(c_int, actor.x),
            @as(c_int, st.z) -% @as(c_int, actor.z),
        ) & 0xFFF;
        st.facing18A = psx.conv(u16, direction);
        st.timer = st.duration;
        c.func_800DEEFC(@ptrCast(st), 4);
    } else if (st.flag9 != 0) {
        const direction: c_int = c.ratan2(
            @as(c_int, st.x) -% @as(c_int, st.targetX),
            @as(c_int, st.z) -% @as(c_int, st.targetZ),
        ) & 0xFFF;
        st.facing188 = psx.conv(u16, direction);
        st.timer = st.duration;
        c.func_800DEEFC(@ptrCast(st), 3);
    } else {
        var bestHeight: c_int = -2147483647 - 1;
        var bestDirection: c_int = -1;
        const source = st.position;
        const x: c_uint = source.raw & 255;
        const z: c_uint = (source.raw >> 16) & 255;
        const angle: c_uint = psx.conv(c_uint, @as(c_int, context.angle26) +% @as(c_int, context.angle14));
        const quadrant: c_uint = (angle & 0xFFF) >> 10;
        var direction: c_int = if ((angle & 0x3FF) > 512)
            psx.conv(c_int, ((quadrant +% 1) & 3) *% 2)
        else
            psx.conv(c_int, quadrant *% 2);

        var i: c_int = 0;
        const bounds: [*]u8 = @ptrFromInt(0x1F8003DC);
        const table: [*]v.D_800F16EC_t = @ptrFromInt(0x1F8003EC);
        while (i < 4) : (i += 1) {
            direction = (direction +% i *% 2) & 7;
            const entry = &table[@intCast(direction)];
            const dx: c_int = @as(c_int, psx.getBits(entry, "dx"));
            const dz: c_int = @as(c_int, psx.getBits(entry, "dz"));
            const nx: c_int = @as(c_int, psx.conv(u8, x +% psx.conv(c_uint, dx)));
            const nz: c_int = @as(c_int, psx.conv(u8, z +% psx.conv(c_uint, dz)));
            if (nx < @as(c_int, bounds[0]) and nz < @as(c_int, bounds[1])) {
                const height: c_int = (c.func_800E4320(nx, nz) << 17) >> 17;
                if (height > bestHeight) {
                    bestDirection = direction;
                    bestHeight = height;
                }
            }
        }

        if (bestDirection < 0) {
            c.func_800DEEFC(@ptrCast(st), 0);
        } else {
            st.facing18A = psx.conv(u16, bestDirection << 9);
            st.timer = st.duration;
            c.func_800DEEFC(@ptrCast(st), 4);
        }
    }
}
