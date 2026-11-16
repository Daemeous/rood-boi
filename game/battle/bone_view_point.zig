//! A point on a battle actor's bone, relative to the camera, through the GTE (pc/src/game/BATTLE/BATTLE.PRG/38C1C.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A1C10 (pc/src/game/BATTLE/BATTLE.PRG/38C1C.c)
/// Writes the bone's origin (endpoint 0), end (1) or middle (other) in camera space to `result`; -1 when the slot or the weapon model is missing.
pub fn boneViewPoint(actorId: c_int, bone_in: c_int, result: [*c]c_ushort, endpoint: c_int) callconv(.c) c_int {
    var bone = bone_in;
    var model: ?*v.D_800F4538_unk68 = null;
    const actor: ?*v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), actorId).*;
    if (actor) |a| model = psx.get(a.unk0.model);
    if (actor == null) {
        const other = psx.at(v.g.vs_field_gimmickInstances(), actorId).* orelse return -1;
        const alt: *v.D_800F4538_t = @ptrCast(other);
        model = psx.get(alt.unk0.model);
    }

    // NULL for a weapon bone without a weapon model: the C reads it only after that -1 return.
    const matrix: ?*v.MATRIX = @ptrCast(c.func_800A1DE8(actorId, bone, psx.cptr(c.MATRIX, v.g.D_800F49B8())));
    if (endpoint == 0) {
        result[0] = psx.conv(c_ushort, matrix.?.t[0]);
        result[1] = psx.conv(c_ushort, matrix.?.t[1]);
        result[2] = psx.conv(c_ushort, matrix.?.t[2]);
        return 0;
    }

    // pad is uninitialised in the original; it only reaches the upper half of GTE register 1.
    var local: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    var transformed: v.VECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    local.vy = 0;
    local.vz = 0;
    if ((bone & 0xF0) == 0x40) {
        const weaponPtr = psx.at(v.g.vs_battle_wepModels(), actorId *% 2).*;
        bone = bone -% 0x3F;
        const weapon = weaponPtr orelse return -1;
        model = @ptrCast(weapon.offsets); // the weapon's header (null: none)
    }

    const arm = psx.at(&model.?.armatures, bone);
    if (endpoint == 1) {
        const u: c_ushort = psx.conv(c_ushort, arm.unk0);
        local.vx = psx.conv(i16, -%@as(c_int, u));
    } else {
        local.vx = psx.conv(i16, @divTrunc(-%arm.unk0, 2));
    }

    // gte_SetRotMatrix, gte_SetTransMatrix, gte_ldv0, gte_rtv0tr2, gte_stlvnl as a value.
    const view = gte.viewOf(matrix.?, gte.no_screen);
    gte.transformTo(&transformed, &view, &local);

    result[0] = psx.conv(c_ushort, transformed.vx);
    result[1] = psx.conv(c_ushort, transformed.vy);
    result[2] = psx.conv(c_ushort, transformed.vz);
    return 0;
}
