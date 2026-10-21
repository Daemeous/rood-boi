//! An actor's jump toward a target: velocity and hang frames from its position (src/BATTLE/BATTLE.PRG/38C1C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A208C (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Starts an actor's jump to a scripted anchor or a tile, setting its velocity and hang frames.
pub fn startJump(actor: ?*v.D_800F4538_t, target: [*c]u8, speed_in: c_int, facing: c_short) callconv(.c) void {
    const a: *v.D_800F4538_t = actor.?;
    const flags: *align(1) c_uint = psx.byteAt(c_uint, a, 8);
    if ((flags.* & 0x70000) != 0) return;

    a.unk0.unk11 = 6;
    const word5ac: *align(1) c_uint = psx.byteAt(c_uint, a, 0x5AC);
    word5ac.* &= ~@as(c_uint, 0x4000000);
    var speed = speed_in;
    if (speed >= @as(c_int, a.unk5B9)) {
        a.unk0.unk11 = 2;
    }
    if (speed < 18) {
        speed = 18;
        word5ac.* |= 0x4000000;
    }

    var x: c_int = 0;
    var y: c_int = 0;
    var z: c_int = 0;
    if (target[1] >= 2) {
        const anchor: *v.SVECTOR = @ptrCast(c.func_800A4A24(@as(c_int, target[1])));
        x = @as(c_int, anchor.vx);
        y = @as(c_int, anchor.vy);
        z = @as(c_int, anchor.vz);
    } else {
        x = @as(c_int, target[0]) *% 128 +% 64;
        z = @as(c_int, target[2]) *% 128 +% 64;
        y = c.func_800A3500(x, z);
    }

    psx.byteAt(c_int, a, 0x5EC).* = psx.byteAt(c_int, target, 0).*;
    a.unk1814 = psx.conv(i16, x);
    a.unk1818 = psx.conv(i16, z);
    x -%= @as(c_int, a.unk0.position.vx);
    y -%= @as(c_int, a.unk0.position.vy);
    z -%= @as(c_int, a.unk0.position.vz);

    var i: c_int = x;
    if (x < 0) i = -%x;
    var dz: c_int = z;
    if (z < 0) dz = -%z;
    if (i < dz) i = dz;
    const minFrames: c_int = @divTrunc(i, speed);

    const accel: c_int = if ((flags.* & 0x400000) != 0) 1 else 6;
    var animation: c_int = if ((flags.* & 0x400000) != 0) 33 else 47;

    var hangFrames: c_int = 0;
    var frames: c_int = 0;
    var fall: c_int = 0;
    var velocity: c_int = 0;

    if (y <= 0) {
        if (a.unk0.unkF != 0) {
            c.func_800AE4FC(@ptrCast(&a.unk0), 12);
        }
        i = @intFromBool(y >= -64);
        if ((flags.* & 0x400000) != 0) i +%= 3;
        hangFrames = @as(c_int, psx.at(v.g.D_800E90AC(), i).*);
        y -%= @as(c_int, psx.at(v.g.D_800E90B4(), i).*);
        while (true) {
            frames = 0;
            fall = 0;
            velocity = 0;
            while (y < fall) : (frames +%= 1) {
                velocity +%= accel;
                fall -%= @divTrunc(velocity, 2);
            }
            frames +%= hangFrames;
            a.unk0.unk34.vx = psx.conv(i16, @divTrunc(x, frames));
            a.unk0.unk34.vz = psx.conv(i16, @divTrunc(z, frames));
            a.unk0.unk34.vy = psx.conv(i16, @divTrunc(-%velocity, 2));
            const vx: c_int = @as(c_int, a.unk0.unk34.vx);
            const vz: c_int = @as(c_int, a.unk0.unk34.vz);
            const vxAbs: c_int = if (vx >= 0) vx else -%vx;
            const vzAbs: c_int = if (vz >= 0) vz else -%vz;
            if (!(vxAbs > speed or vzAbs > speed)) break;
            hangFrames +%= 1;
            y -%= @divTrunc((hangFrames +% 1) *% accel, 2);
        }
    } else {
        i = 2;
        if ((flags.* & 0x400000) != 0) i = 5;
        hangFrames = @as(c_int, psx.at(v.g.D_800E90AC(), i).*);
        y +%= @as(c_int, psx.at(v.g.D_800E90B4(), i).*);
        if ((flags.* & 0x400000) != 0 and y > 128) y = 128;
        frames = 0;
        fall = 0;
        velocity = 0;
        while (fall < y) : (frames +%= 1) {
            velocity +%= accel;
            fall +%= @divTrunc(velocity, 2);
        }
        frames +%= hangFrames;
        if (frames < minFrames) {
            hangFrames +%= @divTrunc(minFrames -% frames, 2) +% 1;
            frames = minFrames;
        }
        a.unk0.unk34.vx = psx.conv(i16, @divTrunc(x, frames));
        a.unk0.unk34.vz = psx.conv(i16, @divTrunc(z, frames));
        a.unk0.unk34.vy = psx.conv(i16, @divTrunc((-%(hangFrames +% 1)) *% accel, 2));
        if ((flags.* & 0x400000) != 0) animation = 35;
    }

    a.unk1816 = psx.conv(i16, frames);
    a.unk181A = 2;
    c.func_800A0204(@as(c_int, a.unk0.unkF), animation, 0, 4);
    flags.* = (flags.* & ~@as(c_uint, 0x70000)) | 0x20000;
    c.func_800A9EB4(@as(c_int, a.unk0.unkF), @as(c_int, facing), 8);
}
