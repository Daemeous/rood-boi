//! Settles a battle actor's point on the ground: writes its height and flags, or returns a blocked code
//! (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A8B34 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Places a point on the ground for the actor in scratchpad slot 0x1F8003BC, writing its height and flags, or returns the blocked code.
pub fn func_800A8B34(point: ?*v.SVECTOR, slot: c_int) callconv(.c) c_int {
    const sb: *v.D_1F8003BC_t = @ptrFromInt(0x1F8003BC);
    const actor: *v.D_800F45E0_t = psx.get(sb.unk18).?;
    const limit: c_int = @as(c_int, sb.unk1C[@intCast(slot & 7)]);

    if ((@as(c_int, psx.getBits(actor, "unk5AC_9")) << 9) != 0) return 0;

    const pos = point.?;
    const gx: c_int = @divTrunc(@as(c_int, pos.vx), 128);
    const gz: c_int = @divTrunc(@as(c_int, pos.vz), 128);
    var elev2: c_int = @as(c_int, c.func_8008DD0C(@as(c_int, pos.vx), @as(c_int, pos.vz)));
    elev2 <<= 17;
    elev2 >>= 17;

    if (limit +% sb.unk0 < elev2) return 0xFF;

    const ent = c.func_800A8D64(psx.cptr(c.SVECTOR, pos), 0);
    if (ent == null) return 0xFF;

    var h: c_int = @as(c_int, c.func_8008DC7C(@as(c_int, pos.vx), @as(c_int, pos.vz)));
    if (h < 0) return 0xFF;
    h <<= 17;
    h >>= 17;

    const r = c.func_800A92B8(gx, gz);
    if (r != 0) h = r;

    if (((ent[0] >> 17) & 1) != 0) {
        if (h >= 0) h = 0xBB8;
    }

    if (h -% @as(c_int, actor.unk1E) < -0x17F) return 0xFF;

    const los = c.func_800A9378(@as(c_int, pos.vx), limit, @as(c_int, pos.vz), 0);
    if (los == 0) {
        pos.pad = 0;
    } else {
        pos.pad = psx.conv(i16, v.g.D_800F49F4().*);
        h = los;
    }
    pos.pad = psx.conv(i16, @as(c_int, pos.pad) | (@as(c_int, v.g.D_800F49F8().*) << 8));

    if (sb.unk0 < elev2 -% h) return 0xFF;
    if (h < sb.unk40 +% limit) return 0xFF;
    if (c.func_800A8E84(@ptrCast(actor), psx.cptr(c.SVECTOR, pos)) == 0) return 0xFF;

    pos.vy = psx.conv(i16, h);
    const diff: c_int = h -% limit;
    var q: c_int = @divTrunc(diff, 64);

    if (q >= 0) return 0;

    if ((diff & 0x3F) != 0) q -%= 1;

    return -%q;
}
