//! The object an actor faces on its next tile, and whether the actor can
//! act on it (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A38E0 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Returns the id of the object the actor faces on its next tile, or -1 when it cannot act on one.
pub fn func_800A38E0(actorId: c_int) callconv(.c) c_int {
    const actor = psx.at(v.g.D_800F4538(), actorId).* orelse return -1;

    if (psx.getBits(actor, "unk5AC_0") != 0) return -1;
    if (psx.getBits(&actor.unk0, "unkA_0") != 0) return -1;
    if (psx.getBits(&actor.unk0, "unkA_3") != 0) return -1;

    var direction: c_int = @as(c_int, actor.unk0.facing) +% 0x200;
    direction &= 0xFFF;
    direction = @divTrunc(direction, 1024);
    v.g.D_800F49E8().* = direction;

    const oldX: c_int = actor.unk0.currentTileX;
    const oldZ: c_int = actor.unk0.currentTileZ;
    var x: c_int = oldX;
    var z: c_int = oldZ;
    var farX: c_int = oldX;
    var farZ: c_int = oldZ;
    const step: c_int = if (direction < 2) -1 else 1;
    if ((direction & 1) != 0) {
        x +%= step;
        farX = oldX +% (step *% 2);
    } else {
        z +%= step;
        farZ = oldZ +% (step *% 2);
    }

    if (x < 0 or z < 0) return -1;

    if (psx.getBits(actor, "unk5AC_3") != 0) {
        x = (x *% 128) +% 64;
        const objectId: c_int = actor.unk0.unk13;
        const object = psx.at(v.g.D_800F45E0(), objectId).*;
        z = (z *% 128) +% 64;
        if (object.?.unk1A != 0) return -1;
        if (c.func_800A3DB4(x, z, @as(c_int, actor.unk0.position.vy) -% 128) == 0) return -1;
        direction = @as(c_int, actor.unk0.position.vy) -% 384;
        if (direction < c.func_800A3BC4(actor.unk0.position.vx, actor.unk0.position.vz)) return -1;
        if (direction < c.func_800A3BC4(x, z)) return -1;
        return objectId;
    }

    const found: c_int = c.func_800A91DC(x, z, 0);
    if (found == 0) return -1;
    const object = psx.at(v.g.D_800F45E0(), found).*;
    const objectId: c_int = found;
    const b16c: u8 = psx.byteAt(u8, object.?, 0x16C).*;
    if ((b16c & 0x30) == 0x10) return -1;
    if (object.?.unk1A != 0) return -1;

    x = (x *% 128) +% 64;
    z = (z *% 128) +% 64;
    farX = (farX *% 128) +% 64;
    farZ = (farZ *% 128) +% 64;

    if (@as(c_int, object.?.unk1E) < @as(c_int, actor.unk0.position.vy) -% 128) return -1;
    if (@as(c_int, actor.unk0.position.vy) +% 64 < @as(c_int, object.?.unk1E)) return -1;

    if ((b16c & 7) < 4) {
        if (c.func_800A3DB4(farX, farZ, @as(c_int, object.?.unk1E)) == 0) return -1;
    } else {
        direction = @as(c_int, actor.unk0.position.vy) -% 384;
        if (direction < c.func_800A3BC4(actor.unk0.position.vx, actor.unk0.position.vz)) return -1;
        if (direction < c.func_800A3BC4(x, z)) return -1;
    }

    if (c.func_800A3C00(@ptrCast(object.?), 1) == null) return objectId;
    return -1;
}
