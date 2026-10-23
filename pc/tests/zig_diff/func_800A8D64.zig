//! zig_diff spec: func_800A8D64 -> func_800A8D64.func_800A8D64 (40564.c).
//! Inputs: an SVECTOR in the arena. Most runs draw vx and vz as a quotient
//! and a remainder, so the C's remainders (vx and vz modulo 128) hit the
//! sum-equals-128 and difference-equals-0 edges of its comparisons; some runs
//! use raw i16 components (wrapping quotients, negative remainders). The
//! _mpdRoomSection3 in the arena has unk0_10 in 0..5; 6 and 7 are not
//! generated because the C then falls off the end without a return (undefined).
//! arg1 is 0, 1, -1 or the int extremes. The stub func_8008B764 returns null
//! about a fifth of the time, otherwise the arena section, and sometimes
//! rewrites the section's unk0_10 (which the C reads back after the call).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A8D64));

pub const classes = [_][:0]const u8{ "sum-eq-128", "diff-eq-0", "other" };

var section: ?*v._mpdRoomSection3 = null;

pub const stubs = struct {
    pub fn func_8008B764(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) ?*v._mpdRoomSection3 {
        _ = arg0;
        _ = arg1;
        _ = arg2;
        if (ctx.value(u8) < 48) return null;
        const s = section orelse return null;
        if (ctx.value(u8) < 64) {
            psx.setBits(s, "unk0_10", @as(u3, @intCast(ctx.value(u8) % 6)));
        }
        return s;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const vec = ctx.alloc(v.SVECTOR);
    const sec = ctx.alloc(v._mpdRoomSection3);
    section = sec;
    psx.setBits(sec, "unk0_10", ctx.range(u3, 0, 5));

    if (ctx.chance(8)) {
        vec.vx = ctx.int(i16);
        vec.vz = ctx.int(i16);
    } else if (ctx.chance(4)) {
        // Non-negative remainders summing to exactly 127 (GCC's form of `< 0x80`).
        const rx = ctx.range(i32, 0, 127);
        vec.vx = @intCast(ctx.range(i32, 0, 254) * 128 + rx);
        vec.vz = @intCast(ctx.range(i32, 0, 254) * 128 + (127 - rx));
    } else {
        // quotient * 128 + remainder, with the remainders steered to the edges
        const rx = ctx.range(i32, -127, 127);
        const rz: i32 = switch (ctx.range(u8, 0, 2)) {
            0 => std.math.clamp(128 - rx, -127, 127),
            1 => rx,
            else => ctx.range(i32, -127, 127),
        };
        const qx = ctx.range(i32, -255, 254);
        const qz = ctx.range(i32, -255, 254);
        vec.vx = @intCast(qx * 128 + rx);
        vec.vz = @intCast(qz * 128 + rz);
    }
    vec.pad = ctx.int(i16);

    const arg1 = ctx.pick(c_int, &.{ 0, 1, -1, std.math.maxInt(c_int), std.math.minInt(c_int) });
    return .{ vec, arg1 };
}

pub fn classify(args: Args) usize {
    const vec = args[0].?;
    const vx: i32 = vec.vx;
    const vz: i32 = vec.vz;
    const rx = vx - @divTrunc(vx, 128) * 128;
    const rz = vz - @divTrunc(vz, 128) * 128;
    if (rx + rz == 128) return 0;
    if (rx - rz == 0) return 1;
    return 2;
}
