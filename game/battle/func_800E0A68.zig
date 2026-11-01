//! A flying or climbing actor's step toward its destination tile: hovers
//! onto ledges, settles on the destination, and arcs over obstacles
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

fn abs(x: c_int) c_int {
    return if (x >= 0) x else -%x;
}

/// The collision grid's tile (scratchpad 0x1F8003C0, rows of 32).
fn gridTile(z: c_int, x: c_int) u16 {
    const rows: [*][32]u16 = @ptrFromInt(@as(usize, @as(*const u32, @ptrFromInt(0x1F8003C0)).*));
    return psx.at(psx.at(rows, z), x).*;
}

fn stepActor(id: u8) *v.movementStepActor {
    return @ptrCast(v.g.D_800F4538()[id].?);
}

/// The C's `fall:` tail.
fn fall(s: *v.movementStepState, mode: c_int) void {
    s.moveY = -4096;
    s.moveZ = 0;
    s.moveX = 0;
    c.func_800DEEFC(@ptrCast(s), mode);
}

/// The C's `recover:` tail.
fn recover(s: *v.movementStepState) void {
    c.func_800E2CCC(@ptrCast(s));
}

/// orig: func_800E0A68 (src/BATTLE/BATTLE.PRG/771A8.c)
/// One movement step of a flying `state` taking `action`: onto a ledge, onto the destination, along a wall (5) or over an obstacle (7).
pub fn func_800E0A68(state: ?*v.movementStepState, action: c_int) callconv(.c) void {
    const s = state.?;
    var step: c_int = undefined;
    var mode: c_int = undefined;

    if (c.func_800E0918(@ptrCast(s), action, 1) != 0) {
        return;
    }
    if (psx.get(s.object).?.flags & 0x200000 == 0) {
        c.func_800E4B18(@ptrCast(s));
        return;
    }
    s.unk7 = 1;
    if (c.func_800E4764(@ptrCast(s), &step, &mode) == 0) {
        return recover(s);
    }
    var x: c_int = @as(c_int, s.destination.p.x) * 128 + 64;
    var z: c_int = @as(c_int, s.destination.p.z) * 128 + 64;
    var angle: c_int = c.ratan2(@as(c_int, s.position.p.x) -% x, @as(c_int, s.z) -% z);
    angle &= 0xFFF;
    if (s.unk0 != 0 and c.func_800E45F4(s.tile.raw, s.destination.raw) < 2 and
        gridTile(s.destination.p.z, s.destination.p.x) & 0x40 != 0)
    {
        const actorY: c_int = stepActor(s.actorId).y;
        var rise: c_int = c.func_800E42E0(s.tile.raw);
        var selectedHeight: c_int = undefined;
        if (actorY >= rise) {
            selectedHeight = rise +% s.height;
        } else if (v.g.D_800F58BC().*.?.unk14 != 0 and v.g.D_800F58BC().*.?.unk14 >= actorY) {
            selectedHeight = v.g.D_800F58BC().*.?.unk14;
        } else {
            selectedHeight = psx.conv(i16, actorY);
        }
        rise = selectedHeight -% s.position.p.y;
        if (abs(rise) < step) {
            angle = @as(c_int, s.tile.p.x) * 128 + 64;
            const centerZ: c_int = @as(c_int, s.tile.p.z) * 128 + 64;
            const centerAngle = c.ratan2(@as(c_int, s.position.p.x) -% angle, @as(c_int, s.z) -% centerZ);
            s.moveY = selectedHeight << 12;
            const heading: c_int = (-%centerAngle +% 4096 * 3 / 4) & 0xFFF;
            const offsetX: c_int = @as(c_int, s.position.p.x) -% angle;
            if (abs(offsetX) < step) {
                s.moveX = angle << 12;
            } else {
                s.moveX = (@as(c_int, s.position.p.x) << 12) +% step *% c.rcos(heading);
            }
            const offsetZ: c_int = @as(c_int, s.z) -% centerZ;
            s.moveZ = if (abs(offsetZ) < step) centerZ << 12 else (@as(c_int, s.z) << 12) +% step *% c.rsin(heading);
            c.func_800DEEFC(@ptrCast(s), 8);
            return;
        }
        s.moveY = rise;
        s.moveZ = 0;
        s.moveX = 0;
        if (s.moveY > 0 and c.func_800E02B4(@ptrCast(s), 0, 0) != 0) {
            s.moveY = 0;
        }
        c.func_800DEEFC(@ptrCast(s), mode);
        return;
    }
    if ((s.tile.raw & 0xFF00FF) == (s.destination.raw & 0xFF00FF)) {
        if (s.unk0 == 0 or c.func_800E02B4(@ptrCast(s), 0, 0) != 0) {
            return recover(s);
        }
        var floorY: c_int = s.height;
        const actor = stepActor(s.actorId);
        if (gridTile(s.destination.p.z, s.destination.p.x) & 0x20 == 0) {
            floorY +%= c.func_800E42E0(s.destination.raw);
        }
        const tileX: c_int = @as(c_int, s.destination.p.x) * 128;
        const deltaX: c_int = @as(c_int, s.position.p.x) - 64;
        s.moveX = tileX -% deltaX;
        const tileZ: c_int = @as(c_int, s.destination.p.z) * 128;
        const deltaZ: c_int = @as(c_int, s.z) - 64;
        s.moveZ = tileZ -% deltaZ;
        s.moveY = floorY -% s.position.p.y;
        if ((s.destination.raw & 0xFF00FF) == (actor.tile.raw & 0xFF00FF)) {
            s.moveY = @as(c_int, actor.y) - (@as(c_int, actor.height) >> 1) - s.position.p.y;
        }
        if (s.moveY > 0 and floorY < @as(c_int, s.position.p.y) +% step) {
            s.moveY = 0;
        }
        if (abs(s.moveX) < step) {
            s.moveX = 0;
        }
        if (abs(s.moveZ) < step) {
            s.moveZ = 0;
        }
        c.func_800DEEFC(@ptrCast(s), mode);
        return;
    }
    c.func_800E3600(@ptrCast(s), angle, 2);
    switch (@as(*const c_int, @ptrFromInt(0x1F8003C8)).*) {
        5 => {
            angle = @as(c_int, s.angle) *% (4096 / 8);
            if (c.func_800E0850(@ptrCast(s), @bitCast(angle)) +% s.height < s.position.p.y) {
                return fall(s, mode);
            }
            angle = -%angle;
            angle +%= 4096 * 3 / 4;
            angle &= 0xFFF;
            s.moveY = 0;
            s.moveX = c.rcos(angle);
            s.moveZ = c.rsin(angle);
            c.func_800DEEFC(@ptrCast(s), mode);
            return;
        },
        7 => {
            var height: c_int = c.func_800E42E0(s.destination.raw) +% s.height;
            const px: c_int = @intCast(s.position.raw & 0xFFFF);
            const py: c_int = @as(c_int, @bitCast(s.position.raw)) >> 16;
            const pz: c_int = s.z;
            var ground: c_int = c.func_800E6A6C(px, pz, x, z) +% s.clearance;
            if (v.g.D_800F58BC().*.?.unk14 != 0) {
                ground = if (ground < v.g.D_800F58BC().*.?.unk14) v.g.D_800F58BC().*.?.unk14 else ground;
            }
            const top: c_int = c.func_800E0850(@ptrCast(s), @bitCast(angle)) +% s.height;
            ground = if (top > ground) ground else top;
            const distance: c_uint = @bitCast(c.vs_gte_rsqrt(@bitCast(c.func_800E4660(x -% px, 0, z -% pz))));
            var value: c_int = height +% ground;
            // value 0 divides by zero in the C too (SIGFPE).
            const denom: c_uint = @as(c_uint, @bitCast(c.vs_gte_rsqrt(@divTrunc(value << 12, value) << 12))) +% 4096;
            if (denom == 0) {
                c.func_800E1238(@ptrCast(s), angle);
                return;
            }
            value = @bitCast((distance << 12) / denom);
            const square: c_uint = @as(c_uint, @bitCast(value)) *% @as(c_uint, @bitCast(value));
            const diff: c_int = py -% ground;
            const offset: c_int = abs(diff);
            if (py +% step < ground) {
                if (step < height -% py) {
                    s.moveY = height -% py;
                } else {
                    s.moveY = 0;
                }
                s.moveX = x -% px;
                s.moveZ = z -% pz;
                c.func_800DEEFC(@ptrCast(s), mode);
                s.unk228 = 0;
                return;
            }
            if (@as(c_uint, @bitCast(step)) >= distance) {
                c.func_800E1238(@ptrCast(s), angle);
                return;
            }
            height = @bitCast(@as(c_uint, @bitCast(offset *% (step -% value) *% (step -% value))) / square +% @as(c_uint, @bitCast(ground)));
            angle = -%angle;
            angle +%= 4096 * 3 / 4;
            angle &= 0xFFF;
            x = (px << 12) +% step *% c.rcos(angle);
            z = (pz << 12) +% step *% c.rsin(angle);
            if (height < ground) {
                height = ground;
            } else if (top < height) {
                if (ground < py) {
                    return fall(s, mode);
                }
                height = top;
            }
            const dy: c_int = height -% py;
            s.unk228 = 0;
            if (step < abs(dy)) {
                s.moveX = (x >> 12) -% px;
                s.moveZ = (z >> 12) -% pz;
                s.moveY = dy;
                c.func_800DEEFC(@ptrCast(s), mode);
            } else {
                s.moveX = x;
                s.moveY = height << 12;
                s.moveZ = z;
                c.func_800DEEFC(@ptrCast(s), 8);
            }
            return;
        },
        else => {},
    }
}
