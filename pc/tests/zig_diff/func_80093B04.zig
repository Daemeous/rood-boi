//! zig_diff spec: func_80093B04 -> func_80093B04.func_80093B04 (2842C.c).
//! Inputs: a 3-word argument array either in its own arena block or
//! overlapping the object the callee returns (so the C's stores into that
//! object change what later argument reads see). The callee's result is a
//! valid arena object; it may also change D_800F227E and the argument words,
//! which the C reads back after the call. D_800F227E starts at the i16
//! edges (32767, -1, -32768, 0), so the increment wraps.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093B04));

pub const classes = [_][:0]const u8{ "separate", "overlap" };

var ret: *v.func_800962E4_t = undefined;
var arg_words: [*c]c_ushort = undefined;
var overlap: bool = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800962E4_t);
    ret = obj;
    overlap = ctx.chance(3);
    const base: [*]u8 = if (overlap) @as([*]u8, @ptrCast(obj)) + 2 else ctx.allocBytes(6, 2);
    const args: [*c]c_ushort = @ptrCast(@alignCast(base));
    args[0] = ctx.pick(u16, &.{ 0, 0xFFFF, 0x8000, 0x7FFF });
    args[1] = ctx.pick(u16, &.{ 0, 0xFFFF, 0x8000, 0x7FFF });
    args[2] = ctx.pick(u16, &.{ 0, 0xFFFF, 0x8000, 0x7FFF });
    v.g.D_800F227E().* = ctx.pick(i16, &.{ 32767, -1, -32768, 0 });
    arg_words = args;
    return .{args};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (overlap) 1 else 0;
}

pub const stubs = struct {
    pub fn func_800962E4(ctx: *zd.Ctx) *v.func_800962E4_t {
        if (ctx.value(u1) == 1) v.g.D_800F227E().* = ctx.value(i16);
        if (ctx.value(u1) == 1) arg_words[1] = ctx.value(u16);
        return ret;
    }
};
