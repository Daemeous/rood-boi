//! Actor transforms through the scratchpad and the GTE
//! (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const gte = common.gte;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800B07DC (src/BATTLE/BATTLE.PRG/44F14.c)
/// The scratchpad offset rotated by the actor's pitch, facing and roll, plus its position.
pub fn offsetToWorld(actor: ?psx.GuestPtr(v.D_800F4538_t)) callconv(.c) void {
    const scratch: *v.actorTransformScratch = @ptrFromInt(0x1F80035C);
    scratch.rotation.vx = actor.?.unk0.pitch;
    scratch.rotation.vy = actor.?.unk0.facing;
    scratch.rotation.vz = actor.?.unk0.roll;
    _ = c.RotMatrixYXZ_gte(psx.cptr(c.SVECTOR, &scratch.rotation), psx.cptr(c.MATRIX, &scratch.matrix));
    scratch.matrix.t[0] = 0;
    scratch.matrix.t[1] = 0;
    scratch.matrix.t[2] = 0;
    gte.SetRotMatrix(&scratch.matrix);
    gte.SetTransMatrix(&scratch.matrix);
    gte.ldv0(&scratch.offset);
    gte.rtv0tr2();
    gte.stlvnl(&scratch.matrix.t);
    scratch.offset.vx = psx.conv(i16, scratch.matrix.t[0]);
    scratch.offset.vy = psx.conv(i16, scratch.matrix.t[1]);
    scratch.offset.vz = psx.conv(i16, scratch.matrix.t[2]);
    scratch.offset.vx = psx.conv(i16, @as(c_int, scratch.offset.vx) +% actor.?.unk0.position.vx);
    scratch.offset.vz = psx.conv(i16, @as(c_int, scratch.offset.vz) +% actor.?.unk0.position.vz);
    scratch.offset.vy = psx.conv(i16, @as(c_int, scratch.offset.vy) +% actor.?.unk0.position.vy);
}
