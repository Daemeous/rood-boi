//! zig_diff spec: func_800BE3D0 -> func_800BE3D0.func_800BE3D0 (4A0A8.c).
//! Inputs: arg0 a plain char (unsigned under -funsigned-char: the
//! edges 0, 1, 127, 128 and 255 often, bit 7 clear and set),
//! the two state flags (0xB0 kept to 0..1 so the table index
//! D_800E9C24[temp_s0] stays inside its two entries; 0xAF any byte), random
//! camera vectors from the stubs (wrapping i32 values for vy), random
//! D_800E9C28/D_800E9C2C bytes, and a valid D_800F4BA4 object from the arena.
//! The C has no branches of its own; the stubs model the camera setup writing
//! both out-vectors and the byte writer func_800BC2E8.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BE3D0));

pub const classes = [_][:0]const u8{ "arg0-high", "arg0-zero", "arg0-low" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4BA4().* = ctx.alloc(v.D_800F4BA4_t);
    const arg0 = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const a: i8 = @bitCast(args[0]);
    if (a < 0) return 0;
    if (a == 0) return 1;
    return 2;
}

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, flag: anytype) u8 {
        const r = ctx.value(u8);
        // The 0xB0 flag indexes D_800E9C24 (2 entries): keep it in 0..1.
        if (flag == 0xB0) return r & 1;
        return r;
    }

    pub fn vs_battle_initialiseCameraFromSpherical(ctx: *zd.Ctx, out0: anytype, out1: anytype, angle: anytype, dist: anytype) void {
        _ = angle;
        _ = dist;
        const a: *v.VECTOR = @ptrCast(@alignCast(out0));
        const b: *v.VECTOR = @ptrCast(@alignCast(out1));
        a.vx = ctx.value(i32);
        a.vy = ctx.value(i32);
        a.vz = ctx.value(i32);
        b.vx = ctx.value(i32);
        b.vy = ctx.value(i32);
        b.vz = ctx.value(i32);
    }

    pub fn func_800BC2E8(ctx: *zd.Ctx, p: anytype, k: anytype) c_int {
        _ = k;
        // The callee may rewrite the byte it is given.
        if (ctx.value(u1) == 1) {
            const b: *u8 = @ptrCast(@alignCast(p));
            b.* = ctx.value(u8);
        }
        return 0;
    }
};
