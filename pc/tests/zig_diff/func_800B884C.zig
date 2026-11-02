//! zig_diff spec: func_800B884C -> func_800B884C.func_800B884C (4A0A8.c).
//! Inputs: a scratch buffer in the arena whose byte 3 selects one of the three
//! D_800E9B4C entries (the C reads only indices 0-2 safely), and an arbitrary
//! short for the unused arg1. The stubs model the callees: func_800BB68C
//! fills the iterator struct, func_800BB788 returns the break value 1, other
//! values (0, 0xFFFF, any u16) and updates the struct, and func_8007C0AC
//! rewrites a D_800E9B4C entry, so the C's next table read depends on the call.
//! The loop is forced to end after a fixed number of stub calls.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const common = @import("common");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B884C));

pub const classes = [_][:0]const u8{ "index0", "index1", "index2" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(8, 1);
    buf[3] = ctx.edge(u8, &.{ 0, 1, 2 });
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ @as([*c]u8, buf), arg1 };
}

pub fn classify(args: Args) usize {
    return @as(usize, args[0][3]);
}

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BB68C(ctx: *zd.Ctx, id: anytype, p: anytype) void {
        _ = id;
        const s: *v.func_800BB68C_t = @ptrCast(@alignCast(p));
        s.unk0 = ctx.value(u8);
        s.unk1 = ctx.value(u8);
    }

    pub fn func_800BB788(ctx: *zd.Ctx, p: anytype) u16 {
        const s: *v.func_800BB68C_t = @ptrCast(@alignCast(p));
        s.unk1 = ctx.value(u8);
        if (ctx.calls > 12) return 1;
        const k = ctx.value(u8);
        if (k < 96) return 1;
        if (k < 128) return 0;
        if (k < 160) return 0xFFFF;
        return ctx.value(u16);
    }

    pub fn func_8007C0AC(ctx: *zd.Ctx, a: c_int, b: c_int) void {
        _ = a;
        _ = b;
        const t = v.g.D_800E9B4C();
        t[ctx.value(u8) % 3] = ctx.value(i32);
    }
};
