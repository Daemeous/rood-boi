//! A point on a battle actor's bone, relative to the camera, through the GTE (src/BATTLE/BATTLE.PRG/38C1C.c).
const psx = @import("common").psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A1C10 (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Writes the bone's origin (endpoint 0), end (1) or middle (other) in camera space to `result`; -1 when the slot or the weapon model is missing.
pub fn boneViewPoint(actorId: c_int, bone_in: c_int, result: [*c]c_ushort, endpoint: c_int) callconv(.c) c_int {
    var bone = bone_in;
    var model: ?*v.D_800F4538_unk68 = null;
    const actor: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), actorId).*;
    if (actor) |a| model = psx.get(a.unk0.unk68);
    if (actor == null) {
        const other = psx.at(v.g.D_800F45E0(), actorId).* orelse return -1;
        const alt: *v.D_800F4538_t = @ptrCast(other);
        model = psx.get(alt.unk0.unk68);
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
        const raw = psx.byteAt(c_uint, weapon, 0x1C).*;
        model = if (raw == 0) null else @ptrFromInt(@as(usize, raw));
    }

    const arm = psx.at(&model.?.armatures, bone);
    if (endpoint == 1) {
        const u: c_ushort = psx.conv(c_ushort, arm.unk0);
        local.vx = psx.conv(i16, -%@as(c_int, u));
    } else {
        local.vx = psx.conv(i16, @divTrunc(-%arm.unk0, 2));
    }

    // gte_SetRotMatrix, gte_SetTransMatrix: the PsyCross register sequence.
    const m = matrix.?;
    c.CTC2(psx.byteAt(c_uint, m, 0).*, 0);
    c.CTC2(psx.byteAt(c_uint, m, 4).*, 1);
    c.CTC2(psx.byteAt(c_uint, m, 8).*, 2);
    c.CTC2(psx.byteAt(c_uint, m, 12).*, 3);
    c.CTC2(psx.byteAt(c_uint, m, 16).*, 4);
    c.CTC2(psx.byteAt(c_uint, m, 20).*, 5);
    c.CTC2(psx.byteAt(c_uint, m, 24).*, 6);
    c.CTC2(psx.byteAt(c_uint, m, 28).*, 7);
    // gte_ldv0
    c.MTC2(psx.byteAt(c_uint, &local, 0).*, 0);
    c.MTC2(psx.byteAt(c_uint, &local, 4).*, 1);
    // gte_rtv0tr2
    _ = c.doCOP2(0x0480012);
    // gte_stlvnl
    transformed.vx = @bitCast(c.MFC2(25));
    transformed.vy = @bitCast(c.MFC2(26));
    transformed.vz = @bitCast(c.MFC2(27));

    result[0] = psx.conv(c_ushort, transformed.vx);
    result[1] = psx.conv(c_ushort, transformed.vy);
    result[2] = psx.conv(c_ushort, transformed.vz);
    return 0;
}
