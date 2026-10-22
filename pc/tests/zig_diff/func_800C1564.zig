//! zig_diff spec: func_800C1564 -> func_800C1564.func_800C1564 (58578.c).
//! Inputs: a func_800C1564_t in the arena with unk0 on each switch case 1-7,
//! on the non-case values 0, 8 and 255 (the default class), the saved flags'
//! unk3 at its wrap edges (0x80 is added to it, so 0x80 and 0xFF wrap), and an
//! arg1 buffer of 16 u16s or null. The C has no ordered comparisons and no
//! zero tests; its only branch is the switch. The four callees are recording
//! stubs that scribble on the flags union, unk2 and arg1 (so the state hash
//! at each call and the restore of unk4 are both observed) and return any int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C1564));

pub const classes = [_][:0]const u8{ "case1", "case2", "case3", "case4", "case5", "case6", "case7", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const o = ctx.alloc(v.func_800C1564_t);
    o.unk0 = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 255 });
    o.unk4._bytes[3] = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    const buf = ctx.allocBytes(32, 2);
    const p: [*c]c_ushort = @ptrCast(@alignCast(buf));
    return .{ o, if (ctx.chance(8)) null else p };
}

pub fn classify(args: Args) usize {
    const o = args[0].?;
    if (o.unk0 >= 1 and o.unk0 <= 7) return o.unk0 - 1;
    return 7;
}

/// The object pointer, whether the stub's parameter is optional or not.
fn obj(p: anytype) *v.func_800C1564_t {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

/// What a real callee may leave behind: the flags union, unk2, and arg1.
fn perturb(ctx: *zd.Ctx, p: anytype, buf: anytype) void {
    const a = obj(p);
    if (ctx.value(u1) == 1) a.unk4._bytes[ctx.value(u2)] = ctx.value(u8);
    if (ctx.value(u1) == 1) a.unk2 = ctx.value(i16);
    if (buf != null) buf[ctx.value(u3)] = ctx.value(u16);
}

pub const stubs = struct {
    pub fn func_800C1034(ctx: *zd.Ctx, arg0: anytype, arg1: anytype) c_int {
        perturb(ctx, arg0, arg1);
        return ctx.value(c_int);
    }
    pub fn func_800C110C(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) c_int {
        _ = arg2;
        perturb(ctx, arg0, arg1);
        return ctx.value(c_int);
    }
    pub fn func_800C123C(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) c_int {
        _ = arg2;
        perturb(ctx, arg0, arg1);
        return ctx.value(c_int);
    }
    pub fn func_800C1384(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) c_int {
        _ = arg2;
        perturb(ctx, arg0, arg1);
        return ctx.value(c_int);
    }
};
