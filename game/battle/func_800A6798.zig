//! Probes whether the actor can stand at a sideways offset from its position (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A6798 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Lifts the actor 0xEC units, tests the ground at a sideways offset in both directions, and restores its position.
pub fn func_800A6798(actor: ?*v.D_800F4538_t, offset: ?*v.SVECTOR, motion: ?*v.SVECTOR) callconv(.c) c_int {
    const a = actor.?;
    var height: c_int = 0;
    var valid: c_int = 0;
    if (@as(c_int, a.unk6EC) <= @as(c_int, v.g.D_800F4B0A().*)) {
        return 0;
    }
    const off = offset.?;
    off.vx = 0;
    off.vz = 0;
    off.vy = psx.conv(i16, @as(c_int, off.vy) +% 0xEC);
    const saved: v.probePosition = @as(*align(1) v.probePosition, @ptrCast(&a.unk0.position)).*;
    const pos = &a.unk0.position;
    pos.vx = psx.conv(i16, @as(c_int, pos.vx) +% @as(c_int, off.vx));
    pos.vy = psx.conv(i16, @as(c_int, pos.vy) +% @as(c_int, off.vy));
    pos.vz = psx.conv(i16, @as(c_int, pos.vz) +% @as(c_int, off.vz));
    psx.setBits(&a.unk0, "unkA_3", 2);
    if (c.func_800A76BC(@ptrCast(a), @ptrCast(motion), &height, 0) != 3 and
        psx.getBits(a, "unk5AC_13") == 0 and height < 5)
    {
        height = c.rsin(0x200);
        height = @divTrunc(height *% @as(c_int, a.unk63C), 4096);
        if (((@as(c_int, v.g.D_800F49F9().*) >> 1) & 1) != 0) {
            off.vz = psx.conv(i16, height);
        } else {
            off.vx = psx.conv(i16, height);
        }
        height = c.func_800A6EE8(psx.cptr(c.SVECTOR, v.g.D_800F4B08()), @as(c_int, off.vx), @as(c_int, off.vz), 1);
        height -%= @as(c_int, v.g.D_800F4B08().vy);
        if (height >= -0x3F) {
            height = c.func_800A6EE8(psx.cptr(c.SVECTOR, v.g.D_800F4B08()), -%@as(c_int, off.vx), -%@as(c_int, off.vz), 1);
            height -%= @as(c_int, v.g.D_800F4B08().vy);
            if (height >= -0x3F) {
                valid = 1;
            }
        }
    }
    psx.setBits(&a.unk0, "unkA_3", 0);
    psx.setBits(&a.unk0, "unk9_6", 0);
    off.vx = 0;
    off.vz = 0;
    @as(*align(1) v.probePosition, @ptrCast(&a.unk0.position)).* = saved;
    return valid;
}
