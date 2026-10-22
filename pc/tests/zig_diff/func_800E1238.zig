//! zig_diff spec: func_800E1238 -> func_800E1238.func_800E1238 (771A8.c).
//! Inputs: a func_800E0850_t in the arena whose unk58 points at a bitfield
//! object (unk8_21 random), a scratchpad mode word at 0x1F8003C8 (5, 7, or
//! another value), an arg1 from a set of edges, and arg0 null when the first
//! check returns early. The callees' return values and the comparison side
//! (below, equal, above) are chosen in prepare through spec vars, so each
//! class is known before the run. func_800E4764 writes both out-params,
//! func_800E3600 can rewrite the mode word and unk188 (both read back by the C),
//! and func_800E0850 returns a value on either side of, or equal to, unk4E.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E1238));

pub const classes = [_][:0]const u8{
    "null-arg",
    "rejected",
    "no-slot",
    "other-mode",
    "m7-ge-clear",
    "m7-ge-set",
    "m7-below-clear",
    "m7-below-set",
    "m5-ge-clear",
    "m5-ge-set",
    "m5-below-clear",
    "m5-below-set",
};

const modeP: *c_int = @ptrFromInt(zd.scratchpad + 0x3C8);

var ret0918: c_int = 0;
var ret4764: c_int = 1;
var sum_mode: u8 = 0; // 0: sum below unk4E, 1: equal, 2: above
var write3600: c_int = 0;

pub const stubs = struct {
    pub fn func_800E0918(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) c_int {
        _ = ctx;
        _ = p;
        _ = a;
        _ = b;
        return ret0918;
    }

    pub fn func_800E4764(ctx: *zd.Ctx, p: anytype, o1: anytype, o2: anytype) c_int {
        _ = p;
        o1.* = ctx.value(c_int);
        o2.* = ctx.value(c_int);
        return ret4764;
    }

    pub fn func_800E0850(ctx: *zd.Ctx, p: anytype, a: anytype) c_int {
        _ = ctx;
        _ = a;
        const o: *v.func_800E0850_t = @ptrCast(@alignCast(p));
        const base = @as(c_int, o.unk4E) - @as(c_int, o.unkBE);
        return switch (sum_mode) {
            0 => base - 1,
            1 => base,
            else => base + 1,
        };
    }

    pub fn func_800E3600(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        const o: *v.func_800E0850_t = @ptrCast(@alignCast(p));
        if (write3600 != 0) modeP.* = write3600;
        if (ctx.value(u2) == 0) o.unk188 = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800E1238_t);
    const arg = ctx.alloc(v.func_800E0850_t);

    const bit: u1 = @intFromBool(ctx.chance(2));
    psx.setBits(obj, "unk8_21", bit);
    psx.put(&arg.unk58, obj);

    arg.unk7 = ctx.int(u8);
    arg.unk4E = ctx.int(i16);
    arg.unkBE = ctx.int(i16);
    arg.unk188 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });

    ret0918 = if (ctx.chance(5)) (if (ctx.chance(2)) @as(c_int, -1) else 1) else 0;
    ret4764 = if (ctx.chance(5)) 0 else 1;
    sum_mode = ctx.range(u8, 0, 2);
    write3600 = if (ctx.chance(4)) (if (ctx.chance(2)) @as(c_int, 5) else 7) else 0;
    modeP.* = ctx.pick(c_int, &.{ 5, 7, 0, 6, -1, 0x7FFFFFFF, 5, 7 });

    var p: ?*v.func_800E0850_t = arg;
    if (ret0918 != 0 and ctx.chance(4)) p = null;

    const a1 = ctx.pick(c_int, &.{ 0, 1, -1, 2048, 4096, 0x7FFFFFFF, -2 });
    return .{ p, a1 };
}

pub fn classify(args: Args) usize {
    const a = args[0] orelse return 0;
    if (ret0918 != 0) return 1;
    if (ret4764 == 0) return 2;
    const mode = modeP.*;
    if (mode != 5 and mode != 7) return 3;
    const obj = psx.get(a.unk58).?;
    const bit = psx.getBits(obj, "unk8_21");
    const below = sum_mode == 0;
    const m5 = mode == 5;
    return 4 + @as(usize, @intFromBool(m5)) * 4 + @as(usize, @intFromBool(below)) * 2 + @as(usize, bit);
}
