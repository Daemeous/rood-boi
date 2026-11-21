//! A battle actor's bone matrix in camera space (pc/src/game/BATTLE/BATTLE.PRG/38C1C.c).
const std = @import("std");
const psx = @import("common").psx;
const pad = @import("common").pad;
const gte = @import("common").gte;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A1DE8 (pc/src/game/BATTLE/BATTLE.PRG/38C1C.c)
/// Returns an actor's bone matrix (or its object, weapon or root offset) placed for the camera, in the scratchpad.
pub fn boneViewMatrix(actorId: c_int, bone: c_int, _: ?*v.MATRIX) callconv(.c) ?*v.MATRIX {
    const scratch: *v.boneMatrixScratch = pad.xform(v.boneMatrixScratch);
    const actor: ?*v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), actorId).*;

    if (actor) |a| {
        if ((bone & 0xF0) == 0x40) {
            const weapon = psx.at(v.g.vs_battle_wepModels(), actorId *% 2).* orelse return null;
            scratch.matrix = psx.at(psx.byteAt(v.MATRIX, weapon, 32), bone -% 63).*;
        } else if (bone == 255) {
            // Root offset: three u16 fields at +0x84C.. halved, scaled by unk183C / 4096.
            scratch.offset.vx = psx.conv(i16, @as(c_int, @as(i16, @bitCast(a.unk704.unk0[41].vx))) >> 1);
            scratch.offset.vy = psx.conv(i16, @as(c_int, @as(i16, @bitCast(a.unk704.unk0[41].vy))) >> 1);
            scratch.offset.vz = psx.conv(i16, @as(c_int, @as(i16, @bitCast(a.unk704.unk0[41].vz))) >> 1);
            const scale: c_int = a.unk183C;
            scratch.offset.vx = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vx) *% scale, 4096));
            scratch.offset.vy = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vy) *% scale, 4096));
            scratch.offset.vz = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vz) *% scale, 4096));
            c.vs_actors_transformRootOffset(@ptrCast(a));
            scratch.matrix.t[0] = @as(c_int, scratch.offset.vx);
            scratch.matrix.t[1] = @as(c_int, scratch.offset.vy);
            scratch.matrix.t[2] = @as(c_int, scratch.offset.vz);
            return &scratch.matrix;
        } else {
            scratch.matrix = psx.at(&a.bones, bone).*;
        }
    } else {
        const object = psx.at(v.g.vs_field_gimmickInstances(), actorId).* orelse return null;
        scratch.matrix = @as(*const v.MATRIX, @ptrCast(psx.at(&object.unk6C, bone))).*;
    }

    const camera = v.g.D_800F49B8();
    const result = &scratch.matrix;
    c.vs_core_mathMultiplyMatrixIntoRight(psx.cptr(c.MATRIX, camera), psx.cptr(c.MATRIX, result));
    // The camera's rotation, translation zero (the translation matrix was uninitialised in the original).
    const view = gte.viewRotation(camera, gte.no_screen);

    scratch.position.vx = psx.conv(i16, scratch.matrix.t[0] -% camera.t[0]);
    scratch.position.vy = psx.conv(i16, scratch.matrix.t[1] -% camera.t[1]);
    scratch.position.vz = psx.conv(i16, scratch.matrix.t[2] -% camera.t[2]);

    gte.transformTo(&scratch.matrix.t, &view, &scratch.position);
    return result;
}
