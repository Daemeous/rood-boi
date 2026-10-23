//! Whether an actor's move is blocked by another actor near its path, with the
//! per-slot exclusion flags kept in the scratchpad (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: func_800A8FD4 (src/BATTLE/BATTLE.PRG/40564.c)
/// Returns 1 when moving the actor by `motion` brings it no closer to any overlapping actor, else 0; marks the actors that overlap in the scratchpad.
pub fn func_800A8FD4(actor: ?*v.D_800F4538_t, motion: ?*v.SVECTOR) callconv(.c) c_int {
    const a = actor.?;
    // scratchpad exclusion bytes: one `excluded` byte per slot i at 0x1F8003BC + i + 4
    const scratch: usize = 0x1F8003BC;

    var k: c_int = 16;
    var clear: usize = scratch + 16;
    while (k >= 0) : ({
        k -%= 1;
        clear -%= 1;
    }) {
        @as(*align(1) v.actorCollisionExclusion, @ptrFromInt(clear)).excluded = 0;
    }

    const flags: c_uint = psx.byteAt(c_uint, a, 0x5AC).*;
    if ((flags & 0x800) != 0) return psx.conv(c_int, flags & 0x800);

    const radius: c_int = psx.byteAt(u16, a, 0x640).*;
    var minY: c_int = @as(c_int, a.unk0.position.vy) -% @as(c_int, psx.byteAt(u16, a, 0x642).*);
    const minX: c_int = @as(c_int, a.unk0.position.vx) -% radius;
    const maxX: c_int = @as(c_int, a.unk0.position.vx) +% radius;
    const minZ: c_int = @as(c_int, a.unk0.position.vz) -% radius;
    const maxZ: c_int = @as(c_int, a.unk0.position.vz) +% radius;
    if ((flags & 8) != 0) minY -%= 128;

    const slots = v.g.D_800F4538();
    var i: c_int = 2;
    while (i < 17) : (i += 1) {
        const other = slots[@intCast(i)] orelse continue;

        const otherFlags: c_uint = psx.byteAt(c_uint, other, 8).*;
        if ((otherFlags & 1) != 0) continue;
        if ((psx.byteAt(c_uint, other, 0x5AC).* & 0x1000000) != 0) continue;
        if (other.unk0.unk13 == 0xFC) continue;
        if ((otherFlags & 0xF000000) == 0x2000000) continue;

        const otherRadius: c_int = psx.byteAt(u16, other, 0x640).*;
        const ox: c_int = other.unk0.position.vx;
        const oy: c_int = other.unk0.position.vy;
        const oz: c_int = other.unk0.position.vz;
        if (ox +% otherRadius < minX) continue;
        if (maxX < ox -% otherRadius) continue;
        if (oz +% otherRadius < minZ) continue;
        if (maxZ < oz -% otherRadius) continue;
        if (oy < minY) continue;
        if (@as(c_int, a.unk0.position.vy) < oy -% @as(c_int, psx.byteAt(u16, other, 0x642).*)) continue;

        @as(*align(1) v.actorCollisionExclusion, @ptrFromInt(scratch + @as(usize, @intCast(i)))).excluded = 1;

        const otherX: i16 = other.unk0.position.vx;
        const actorX: i16 = a.unk0.position.vx;
        var dx: c_int = @as(c_int, otherX) -% @as(c_int, actorX);
        dx *%= dx;
        const otherZ: i16 = other.unk0.position.vz;
        const actorZ: i16 = a.unk0.position.vz;
        var dz: c_int = @as(c_int, otherZ) -% @as(c_int, actorZ);
        dz *%= dz;
        const oldDistance: c_int = dx +% dz;

        dx = @as(c_int, otherX) -% (@as(c_int, actorX) +% @as(c_int, motion.?.vx));
        dx *%= dx;
        dz = @as(c_int, otherZ) -% (@as(c_int, actorZ) +% @as(c_int, motion.?.vz));
        dz *%= dz;
        dx +%= dz;
        if (dx < oldDistance) return 0;
    }
    return 1;
}
