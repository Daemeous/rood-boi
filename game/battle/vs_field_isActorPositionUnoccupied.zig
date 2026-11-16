//! Tells whether an actor may stand at a position: no other live, unexcluded actor's footprint covers it (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_40564");

/// orig: vs_field_isActorPositionUnoccupied (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Returns 0 when another live, unexcluded actor's box around its position covers `position`, else 1.
pub fn vs_field_isActorPositionUnoccupied(actor: ?*v.D_800F4538_t, position: ?*v.SVECTOR) callconv(.c) c_int {
    const scratch: *v.actorCollisionExclusion = @ptrFromInt(pad.addr(0x3BC));
    const a = actor.?;
    if (a.unk0.unkF == 0) {
        if ((psx.fieldAt(c_uint, &a._bits_unk5AC_0, 0).* & 0x800) != 0) return 1;
        if ((psx.fieldAt(c_uint, &a._bits_unk5AC_0, 0).* & 0x1000) != 0) return 1;
        var i: c_int = 2;
        while (i < 17) : (i +%= 1) {
            const other = v.g.vs_actors_actorRecords()[@intCast(i)];
            if (other == null or other.? == a) continue;
            const o = other.?;
            const flags: c_int = @bitCast(psx.fieldAt(c_uint, &o.unk0._bits_skip, 0).*);
            if ((flags & 1) != 0) continue;
            if ((psx.fieldAt(c_uint, &o._bits_unk5AC_0, 0).* & 0x1000000) != 0) continue;
            if (o.unk0.unk13 == 0xFC) continue;
            if ((flags & 0xF000000) == 0x2000000) continue;
            if (psx.byteAt(v.actorCollisionExclusion, scratch, @as(isize, i)).excluded != 0) continue;
            const radius: c_int = @as(c_int, psx.fieldAt(u16, &o.unk640, 0).*);
            // position is only dereferenced from here on, as in the C.
            const pos = position.?;
            if (@as(c_int, o.unk0.position.vx) +% radius < @as(c_int, pos.vx)) continue;
            if (@as(c_int, pos.vx) < @as(c_int, o.unk0.position.vx) -% radius) continue;
            if (@as(c_int, o.unk0.position.vz) +% radius < @as(c_int, pos.vz)) continue;
            if (@as(c_int, pos.vz) < @as(c_int, o.unk0.position.vz) -% radius) continue;
            if (@as(c_int, o.unk0.position.vy) <
                @as(c_int, a.unk0.position.vy) -% @as(c_int, psx.fieldAt(u16, &a.unk640, 2).*)) continue;
            if (@as(c_int, a.unk0.position.vy) <
                @as(c_int, o.unk0.position.vy) -% @as(c_int, psx.fieldAt(u16, &o.unk640, 2).*)) continue;
            return 0;
        }
    }
    return 1;
}
