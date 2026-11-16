//! Attack trace (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: clear_of_obstacles (pc/src/battle_movement.c)
/// 1 unless one of the room's obstacle points (vs_field_collectMovingPlatformPositions) lies within
/// 0x40 / 0x20 / 0x40 of (x, y, z) (obstacle Y raised by 0x20).
fn clearOfObstacles(x: c_int, y: c_int, z: c_int) c_int {
    var buffer: [8][2]c_uint = undefined;
    const count = c.vs_field_collectMovingPlatformPositions(&buffer);
    if (count > 8) c.pc_unimplemented("obstacle point buffer overflow");
    // Past 8 points the original reads beyond its buffer: not bounds-checked.
    const points: [*][2]c_uint = &buffer;
    var i: c_int = 0;
    while (i < count) : (i += 1) {
        const point = points[@intCast(i)];
        var dx: c_int = @as(c_int, @bitCast(point[0] & 0xffff)) -% x;
        var dy: c_int = (@as(c_int, @bitCast(point[0])) >> 16) +% 0x20 -% y;
        var dz: c_int = psx.conv(i16, point[1]) -% z;
        if (dx < 0) dx = -%dx;
        if (dy < 0) dy = -%dy;
        if (dz < 0) dz = -%dz;
        if (dx < 0x40 and dy < 0x20 and dz < 0x40) return 0;
    }
    return 1;
}

/// orig: vs_field_traceActorAndTerrainCollision (pc/src/battle_movement.c)
/// Traces an attack from `from` toward `to` in 32 steps of (to - from) >> 5.
/// Stops at the first actor (not `self`, not hidden by flags 0xEA, not
/// knocked down) whose bounds contain the point while in front of the start,
/// at an obstacle, or below the floor: stores the point (pad = actor id + 1,
/// or 0) in `out` and returns its address; 0 when nothing was hit.
pub fn traceAttack(from: [*]const c_uint, to: [*]const c_uint, out: [*]c_uint, self: c_int) callconv(.c) c_int {
    var x: c_int = psx.conv(i16, from[0]);
    var y: c_int = @as(c_int, @bitCast(from[0])) >> 16;
    var z: c_int = psx.conv(i16, from[1]);
    const start_x: c_int = psx.conv(i16, x);
    const start_z: c_int = psx.conv(i16, z);
    const step_x: c_int = (@as(c_int, psx.conv(i16, to[0])) -% x) >> 5;
    const step_y: c_int = ((@as(c_int, @bitCast(to[0])) >> 16) -% y) >> 5;
    const step_z: c_int = (@as(c_int, psx.conv(i16, to[1])) -% z) >> 5;
    var steps: c_int = 0;
    while (true) {
        x +%= step_x;
        y +%= step_y;
        z +%= step_z;
        var hit: c_int = 0;
        var id: c_int = 0;
        while (id < 16 and hit == 0) : (id += 1) {
            const index: usize = @intCast(id);
            const actor: *ai.vs_battle_actor = @ptrCast(@alignCast(v.g.vs_battle_actors()[index] orelse continue));
            if ((@as(u8, @truncate(actor.allegiance)) & 0xea) != 0 or id == self) continue;
            var body: [*]u8 = undefined;
            var reach: c_int = 0x3f;
            var height: c_int = undefined;
            if (v.g.vs_field_gimmickInstances()[index]) |engaged| {
                body = @ptrCast(engaged);
                if (body[0x12] == 0 or body[0x1a] != 0) continue;
                height = 0x80;
            } else {
                const object = v.g.vs_actors_actorRecords()[index] orelse continue;
                body = @ptrCast(object);
                const pose = psx.fieldAt(u8, &object.unk0._bits_skip, 3).* & 0xf;
                reach = object.unk63C;
                if (pose == 1 or pose == 2 or (psx.fieldAt(u8, &object._bits_unk5AC_0, 3).* & 1) == 1) continue;
                height = object.menuCameraHeightOffset;
            }
            const position: c_uint = psx.byteAt(c_uint, body, 0x1c).*;
            height >>= 1;
            const px: c_int = psx.conv(i16, position);
            const py: c_int = (@as(c_int, @bitCast(position)) >> 16) -% height;
            const pz: c_int = psx.byteAt(u16, body, 0x20).*;
            const ahead: c_int = (px -% start_x) *% step_x +% step_z *% (pz -% start_z);
            var dx = px -% x;
            var dy = py -% y;
            var dz = pz -% z;
            if (dx < 0) dx = -%dx;
            if (dy < 0) dy = -%dy;
            if (dz < 0) dz = -%dz;
            if (ahead < 0) continue;
            if ((dx < reach) & (dy < height) & (dz < reach)) hit = id + 1;
        }
        const pad = hit;
        if (hit == 0 and clearOfObstacles(x, y, z) != 0 and
            y < @as(c_int, @bitCast(psx.conv(c_uint, c.vs_field_floorHeightAt(x, z)) << 17)) >> 17)
        {
            steps += 1;
            if (steps < 32) continue;
            return 0;
        }
        out[0] = @as(c_uint, @bitCast(x & 0xffff)) | (@as(c_uint, @bitCast(y)) << 16);
        out[1] = @as(c_uint, @bitCast(z & 0xffff)) | (@as(c_uint, @bitCast(pad)) << 16);
        return @bitCast(psx.addr(out));
    }
}
