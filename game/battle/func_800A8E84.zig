//! Tells whether an actor may stand at a position: no other live, unexcluded actor's footprint covers it (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: func_800A8E84 (src/BATTLE/BATTLE.PRG/40564.c)
/// Returns 0 when another live, unexcluded actor's box around its position covers `position`, else 1.
pub fn func_800A8E84(actor: ?*v.D_800F4538_t, position: ?*v.SVECTOR) callconv(.c) c_int {
    const scratch: *v.actorCollisionExclusion = @ptrFromInt(0x1F8003BC);
    const a = actor.?;
    if (a.unk0.unkF == 0) {
        if ((psx.byteAt(c_uint, a, 0x5AC).* & 0x800) != 0) return 1;
        if ((psx.byteAt(c_uint, a, 0x5AC).* & 0x1000) != 0) return 1;
        var i: c_int = 2;
        while (i < 17) : (i +%= 1) {
            const other = v.g.D_800F4538()[@intCast(i)];
            if (other == null or other.? == a) continue;
            const o = other.?;
            const flags: c_int = @bitCast(psx.byteAt(c_uint, o, 8).*);
            if ((flags & 1) != 0) continue;
            if ((psx.byteAt(c_uint, o, 0x5AC).* & 0x1000000) != 0) continue;
            if (o.unk0.unk13 == 0xFC) continue;
            if ((flags & 0xF000000) == 0x2000000) continue;
            if (psx.byteAt(v.actorCollisionExclusion, scratch, @as(isize, i)).excluded != 0) continue;
            const radius: c_int = @as(c_int, psx.byteAt(u16, o, 0x640).*);
            // position is only dereferenced from here on, as in the C.
            const pos = position.?;
            if (@as(c_int, o.unk0.position.vx) +% radius < @as(c_int, pos.vx)) continue;
            if (@as(c_int, pos.vx) < @as(c_int, o.unk0.position.vx) -% radius) continue;
            if (@as(c_int, o.unk0.position.vz) +% radius < @as(c_int, pos.vz)) continue;
            if (@as(c_int, pos.vz) < @as(c_int, o.unk0.position.vz) -% radius) continue;
            if (@as(c_int, o.unk0.position.vy) <
                @as(c_int, a.unk0.position.vy) -% @as(c_int, psx.byteAt(u16, a, 0x642).*)) continue;
            if (@as(c_int, a.unk0.position.vy) <
                @as(c_int, o.unk0.position.vy) -% @as(c_int, psx.byteAt(u16, o, 0x642).*)) continue;
            return 0;
        }
    }
    return 1;
}
