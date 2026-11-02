//! zig_diff spec: func_800B6778 -> func_800B6778.func_800B6778 (4A0A8.c).
//! Inputs: a script of 0..6 opcodes followed by the end opcode 0x0F, built
//! from a length table D_800E9C30 the spec fills (1..4 per opcode, never 0:
//! the C would loop forever). The D_800E9BE8 marker (4 bytes, memcmp runs as
//! libc's) is placed at an opcode boundary half the time, sometimes with its
//! last byte off by one.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B6778));

pub const classes = [_][:0]const u8{ "end", "marker" };

var has_marker = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const lengths = v.g.D_800E9C30();
    for (0..256) |i| lengths[i] = ctx.range(u8, 1, 4);
    const marker = v.g.D_800E9BE8();
    marker[0] = ctx.edge(u8, &.{ 0x10, 0x20, 0x7F });
    const buf = ctx.allocBytes(64, 1);
    const n = ctx.range(usize, 0, 6);
    const at = if (ctx.chance(2)) ctx.range(usize, 0, n) else 99;
    var p: usize = 0;
    has_marker = false;
    for (0..n) |i| {
        if (i == at) {
            @memcpy(buf[p .. p + 4], marker[0..4]);
            if (!ctx.chance(4)) {
                has_marker = true;
                break;
            }
            buf[p + 3] +%= 1;
        } else {
            buf[p] = ctx.int(u8);
            if (buf[p] == 0x0F or buf[p] == marker[0]) buf[p] = 0x11 + @as(u8, @intCast(i)) * 2;
        }
        p += lengths[buf[p]];
    }
    if (!has_marker) buf[p] = 0x0F;
    return .{buf};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(has_marker);
}
