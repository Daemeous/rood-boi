//! Battle camera-object setup for the acting actor's tile: the mode 2 hold, the mode 1 step, and the normal move (src/BATTLE/BATTLE.PRG/3A1A0.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A41D0 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Sets up the object of the acting actor's tile: mode 2 holds it, mode 1 places it one tile over, otherwise it turns it one step.
pub fn func_800A41D0(actor: ?*v.D_800F4538_t, mode: c_int) callconv(.c) void {
    // The C reads the object pointer before the mode tests; the element index is not bounded.
    const object: ?*v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), v.g.D_800E90A8().*).*;

    if (mode == 2) {
        const obj = object.?;
        switch (psx.byteAt(u8, obj, 0x16C).* & 7) {
            0, 1 => obj.unk1A = 0xF9,
            2 => obj.unk1A = 0xFF,
            3 => obj.unk1A = 0xFE,
            else => {},
        }
        psx.byteAt(u8, obj, 0x170).* = 0;
        _ = c.func_800E68A0(@ptrCast(obj));
        return;
    }

    if (mode == 1) {
        const act = actor.?;
        var step: c_int = 1;
        var x: c_int = @as(c_int, act.unk0.currentTileX);
        var z: c_int = @as(c_int, act.unk0.currentTileZ);
        if (v.g.D_800F49E8().* < 2) {
            step = -1;
        }
        if ((v.g.D_800F49E8().* & 1) != 0) {
            x +%= step;
        } else {
            z +%= step;
        }
        step = v.g.D_800F49E8().*;
        const obj = object.?;
        obj.unk5C = psx.conv(u8, x);
        obj.unk5E = psx.conv(u8, z);
        obj.unk1C = psx.conv(i16, (x << 7) +% 0x40);
        obj.unk20 = psx.conv(i16, (z << 7) +% 0x40);
        obj.unk26 = psx.conv(i16, (@as(c_int, obj.unk26) +% (step << 10)) & 0xFFF);
        // Uninitialised in the original; the callee fills both entries.
        var points = std.mem.zeroes([2]v.SVECTOR);
        _ = c.func_800A190C(@as(c_int, obj.unk12), @as(c_int, 0xF0), psx.cptr(c.SVECTOR, &points[0]), @as(c_int, 1));
        _ = c.func_800A190C(@as(c_int, obj.unk12), @as(c_int, 0xF1), psx.cptr(c.SVECTOR, &points[1]), @as(c_int, 1));
        obj.unk1E = psx.conv(i16, @divTrunc(@as(c_int, points[0].vy) +% points[1].vy, 2) +% 0x3C);
        obj.unk1A = 0xFC;
        obj.unk36 = 0x18;
        obj.unk12 = 0xFF;
        psx.setBits(act, "unk5AC_3", 0);
        return;
    }

    // object == NULL short-circuits: the actor is only dereferenced here.
    if (object == null or psx.getBits(object.?, "unk9_0") != 0) {
        const act = actor.?;
        _ = c.func_800A0204(@as(c_int, act.unk0.unkF), @as(c_int, 1), @as(c_int, 0), @as(c_int, 8));
        return;
    }

    const act = actor.?;
    const obj = object.?;
    _ = c.func_800E4C28(@as(c_int, obj.unk5C), @as(c_int, obj.unk5E));
    _ = c.func_800E68A0(@ptrCast(obj));
    if ((psx.byteAt(c_int, obj, 0x16C).* & 0x30) == 0x20) {
        _ = c.func_800E68A0(@ptrCast(obj));
    }

    var step: c_int = -0x80;
    if (v.g.D_800F49E8().* < 2) {
        step = 0x80;
    }
    if ((v.g.D_800F49E8().* & 1) != 0) {
        obj.unk34 = psx.conv(i16, @as(c_int, obj.unk34) +% step);
    } else {
        obj.unk38 = psx.conv(i16, @as(c_int, obj.unk38) +% step);
    }

    const facing: c_int = (@as(c_int, psx.conv(u16, obj.unk26)) +% ((4 -% v.g.D_800F49E8().*) << 10)) & 0xFFF;
    var direction: c_int = facing +% 0x200;
    direction &= 0xFFF;
    obj.unk26 = psx.conv(i16, facing);
    direction = @divTrunc(direction, 1024);
    obj.unk26 = psx.conv(i16, direction *% 1024);
    obj.unk5C = 0xFF;
    obj.unk1A = 8;
    obj.unk12 = act.unk0.unkF;
    act.unk0.unk13 = psx.conv(u8, v.g.D_800E90A8().*);
    psx.setBits(act, "unk5AC_3", 1);
}
