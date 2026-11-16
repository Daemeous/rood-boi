//! Copies a saved bone matrix into the bone table and an actor, transformed by a parent matrix (pc/src/game/BATTLE/BATTLE.PRG/49268.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const gte = common.gte;
const v = @import("views_49268");
const c = @import("c_49268");

/// orig: vs_actors_applyLockedBoneTransform (pc/src/game/BATTLE/BATTLE.PRG/49268.c)
/// Takes the saved bone matrix, rotates and translates it by the parent matrix, and stores it into the bone table and the actor.
pub fn vs_actors_applyLockedBoneTransform(actor_in: ?*v.D_800F4538_t, parent: ?*v.MATRIX, bone: c_int, saved: c_int) callconv(.c) void {
    const scratch: *v.boneTransformScratch = @ptrFromInt(pad.addr(0x35C));
    const matrix: *v.MATRIX = @ptrFromInt(pad.addr(0x3C0));

    const actor = actor_in.?;
    // The saved bone matrices are the 32-byte records from unk1890.
    const saved_matrix = psx.fieldAt(v.MATRIX, &actor.unk1890, @as(isize, saved -% 1) << 5);
    matrix.* = saved_matrix.*;

    c.vs_core_mathPremultiplyRotationMatrix(psx.cptr(c.MATRIX, parent.?), psx.cptr(c.MATRIX, matrix));

    // gte_SetRotMatrix(parent); gte_SetTransMatrix(parent);
    const view = gte.viewOf(parent.?, gte.no_screen);

    scratch.translation.vx = psx.conv(i16, psx.fieldAt(u16, &saved_matrix.t[0], 0).*);
    scratch.translation.vy = psx.conv(i16, psx.fieldAt(u16, &saved_matrix.t[1], 0).*);
    scratch.translation.vz = psx.conv(i16, psx.fieldAt(u16, &saved_matrix.t[2], 0).*);

    // gte_ldv0(0x1F8003A8); gte_rtv0tr2(); gte_stlvnl(0x1F8003D4);
    gte.transformTo(@as(*[3]c_int, @ptrFromInt(pad.addr(0x3D4))), &view, @as(*const v.SVECTOR, @ptrFromInt(pad.addr(0x3A8))));

    // *psx_to_ptr(MATRIX*, boneOffset + psx_to_int(int, table)) = *matrix;
    const bone_offset: c_int = bone << 5;
    const table = v.g.vs_actors_skeletonPose();
    const table_addr: c_uint = psx.conv(c_uint, bone_offset) +% psx.addr(table);
    @as(*align(1) v.MATRIX, @ptrFromInt(@as(usize, table_addr))).* = matrix.*;

    // actor->bones[bone] = *matrix;
    psx.fieldAt(v.MATRIX, &actor.bones, bone_offset).* = matrix.*;
}
