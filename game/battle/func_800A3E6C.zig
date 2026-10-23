//! Starts an actor's move one tile along the direction its object selects
//! (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A3E6C (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Sets up the actor's object step and moves the actor one tile; returns -1 when it cannot.
pub fn func_800A3E6C(actorId: c_int) callconv(.c) c_int {
    const actor: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), actorId).*.?;
    if ((psx.byteAt(c_uint, actor, 0x5AC).* & 0x100000) != 0) {
        return -1;
    }
    const objectId = c.func_800A38E0(actorId);
    if (objectId == -1) {
        return -1;
    }
    const object: *v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), objectId).*.?;
    object.unk3A = psx.conv(i16, v.g.D_800F49E8().*);
    v.g.D_800E90A8().* = objectId;
    var height: c_int = @as(c_int, actor.unk0.position.vy) -% @as(c_int, object.unk1E);
    height = @divTrunc(height, 64);
    height +%= 1;

    object.unk34 = 0;
    object.unk38 = 0;
    const mode = psx.byteAt(u8, object, 0x16C).* & 7;
    var step: c_int = 0;
    if (mode <= 3) {
        if (mode <= 1) {
            const idx: c_int = @as(c_int, psx.byteAt(u8, object, 0x16E).*) *% 4 +% v.g.D_800F49E8().*;
            psx.byteAt(u8, object, 0x16E).* = psx.at(v.g.D_800E8FD0(), idx).*;
        }
        if (mode == 3) {
            step = 0x20;
            object.unk34 = 0;
            object.unk38 = 0;
            if (v.g.D_800F49E8().* < 2) step = -0x20;
        } else {
            step = 0x80;
            if (v.g.D_800F49E8().* < 2) step = -0x80;
        }
        if ((v.g.D_800F49E8().* & 1) != 0) {
            object.unk34 = psx.conv(i16, step);
        } else {
            object.unk38 = psx.conv(i16, step);
        }
        // shared tail of cases 2 and 3 (the C's `move:` label)
        object.unk36 = 0;
        _ = c.func_800AE6C0(@ptrCast(actor), 0x2C, 0x2D);
        _ = c.func_800A0204(actorId, height +% 0x4F, 0, 4);
        psx.byteAt(c_uint, actor, 0x5AC).* |= 0x100000;
        _ = c.func_800E4C1C(
            @divTrunc(@as(c_int, object.unk1C) +% @as(c_int, object.unk34), 128),
            @divTrunc(@as(c_int, object.unk20) +% @as(c_int, object.unk38), 128),
        );
    } else if (mode >= 4 and mode <= 6) {
        if ((psx.byteAt(c_uint, actor, 0x5AC).* & 8) != 0) {
            _ = c.func_800A0204(actorId, 0x4C, 0, 4);
            _ = c.func_800AE6C0(@ptrCast(actor), 0x2E, 0x2F);
            psx.byteAt(c_uint, actor, 0x5AC).* |= 0x100000;
        } else {
            _ = c.func_800A0204(actorId, height +% 0x47, 0, 4);
            _ = c.func_800AE6C0(@ptrCast(actor), 0x2C, 0x2D);
            psx.byteAt(c_uint, actor, 0x5AC).* |= 0x100000;
        }
        var dx: c_int = 0;
        var dz: c_int = 0;
        step = 1;
        if (v.g.D_800F49E8().* < 2) step = -1;
        if ((v.g.D_800F49E8().* & 1) != 0) {
            dx = step;
        } else {
            dz = step;
        }
        _ = c.func_800E4C1C(
            @as(c_int, actor.unk0.currentTileX) +% dx,
            @as(c_int, actor.unk0.currentTileZ) +% dz,
        );
    }

    // The 4-byte tile copy: x, y, z, direction, as the four bytes of the actor's tile fields.
    step = 1;
    var tile: [4]u8 = .{
        actor.unk0.currentTileX,
        actor.unk0.unk5D,
        actor.unk0.currentTileZ,
        actor.unk0.unk5F,
    };
    if (v.g.D_800F49E8().* < 2) step = -1;
    if ((v.g.D_800F49E8().* & 1) != 0) {
        tile[0] = psx.conv(u8, @as(c_int, tile[0]) +% step);
    } else {
        tile[2] = psx.conv(u8, @as(c_int, tile[2]) +% step);
    }
    _ = c.func_800AA454(actorId, @ptrCast(&tile), 4);

    // pad is uninitialised in the original; it is zeroed here.
    var position: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    position.vx = psx.conv(i16, (@as(c_int, actor.unk0.currentTileX) << 7) +% 0x40);
    position.vz = psx.conv(i16, (@as(c_int, actor.unk0.currentTileZ) << 7) +% 0x40);
    position.vy = actor.unk0.position.vy;
    _ = c.func_800AA620(actorId, @ptrCast(&position), 4);
    // The C falls off the end after a void call: eax is the callee's leftover, which
    // the port does not model; 0 is returned.
    return 0;
}
