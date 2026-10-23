//! Copies a saved bone matrix into the bone table and an actor, transformed by a parent matrix (src/BATTLE/BATTLE.PRG/49268.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_49268");
const c = @import("c_49268");

/// orig: func_800B26B8 (src/BATTLE/BATTLE.PRG/49268.c)
/// Takes the saved bone matrix, rotates and translates it by the parent matrix, and stores it into the bone table and the actor.
pub fn func_800B26B8(actor: ?*anyopaque, parent: ?*v.MATRIX, bone: c_int, saved: c_int) callconv(.c) void {
    const scratch: *v.boneTransformScratch = @ptrFromInt(0x1F80035C);
    const matrix: *v.MATRIX = @ptrFromInt(0x1F8003C0);

    // *matrix = ((savedBoneMatrix*)(unsigned)((saved << 5) + (int)psx_addr(actor)))->matrix;
    const src_addr: c_uint = psx.conv(c_uint, saved << 5) +% psx.addr(actor.?);
    matrix.* = @as(*align(1) const v.savedBoneMatrix, @ptrFromInt(@as(usize, src_addr))).matrix;

    c.func_800B196C(psx.cptr(c.MATRIX, parent.?), psx.cptr(c.MATRIX, matrix));

    // gte_SetRotMatrix(parent); gte_SetTransMatrix(parent);
    const words: *align(1) const [8]c_uint = @ptrCast(parent.?);
    c.CTC2(words[0], 0);
    c.CTC2(words[1], 1);
    c.CTC2(words[2], 2);
    c.CTC2(words[3], 3);
    c.CTC2(words[4], 4);
    c.CTC2(words[5], 5);
    c.CTC2(words[6], 6);
    c.CTC2(words[7], 7);

    const saved_off: c_int = (saved -% 1) << 5;
    scratch.translation.vx = psx.conv(i16, psx.byteAt(u16, actor.?, @as(isize, saved_off) + 0x18A4).*);
    scratch.translation.vy = psx.conv(i16, psx.byteAt(u16, actor.?, @as(isize, saved_off) + 0x18A8).*);
    scratch.translation.vz = psx.conv(i16, psx.byteAt(u16, actor.?, @as(isize, saved_off) + 0x18AC).*);

    // gte_ldv0(0x1F8003A8); gte_rtv0tr2(); gte_stlvnl(0x1F8003D4);
    const tr: [*]align(1) const c_uint = @ptrFromInt(0x1F8003A8);
    c.MTC2(tr[0], 0);
    c.MTC2(tr[1], 1);
    _ = c.doCOP2(0x0480012);
    const out: [*]align(1) c_uint = @ptrFromInt(0x1F8003D4);
    out[0] = c.MFC2(25);
    out[1] = c.MFC2(26);
    out[2] = c.MFC2(27);

    // *psx_to_ptr(MATRIX*, boneOffset + psx_to_int(int, table)) = *matrix;
    const bone_offset: c_int = bone << 5;
    const table = v.g.D_800F3C58();
    const table_addr: c_uint = psx.conv(c_uint, bone_offset) +% psx.addr(table);
    @as(*align(1) v.MATRIX, @ptrFromInt(@as(usize, table_addr))).* = matrix.*;

    // destination->matrix = *matrix;
    const dest_addr: c_uint = psx.conv(c_uint, bone_offset) +% psx.addr(actor.?);
    @as(*align(1) v.actorBoneMatrix, @ptrFromInt(@as(usize, dest_addr))).matrix = matrix.*;
}
