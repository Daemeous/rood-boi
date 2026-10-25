//! zig_diff spec: func_8009F298 -> func_8009F298.func_8009F298 (30DB0.c).
//! Inputs: arg0 is a table index in 0..16 (D_800F4538 has 17 entries), and that
//! entry is null, another arena object, or the prepared object. arg1 is null or
//! an arena SVECTOR. arg2 is 0, 1, 2 or -1, so the `== 1` test sees both sides.
//! func_8009EFEC is a recording stub that rewrites the SVECTOR and sometimes
//! the table entry the C reads back. func_800AD494 returns null, an arena
//! pointer, or a pointer with and without the sign bit through its out-parameter,
//! so the `sp10 != 0` test sees both outcomes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F298));

pub const classes = [_][:0]const u8{ "arg2-not-1", "arg2-1-null-entry", "arg2-1-valid-entry" };

var obj: ?*v.D_800F4538_t = null;
var other: ?*v.D_800F4538_t = null;

pub fn prepare(ctx: *zd.Ctx) Args {
    obj = ctx.alloc(v.D_800F4538_t);
    other = ctx.alloc(v.D_800F4538_t);
    const arg0 = ctx.range(c_int, 0, 16);
    const arg1: ?*v.SVECTOR = if (ctx.chance(4)) null else ctx.alloc(v.SVECTOR);
    const arg2 = ctx.pick(c_int, &.{ 0, 1, 2, -1 });
    const tbl = v.g.D_800F4538();
    const idx: usize = @as(usize, @intCast(arg0));
    if (ctx.chance(3)) {
        tbl[idx] = null;
    } else if (ctx.chance(2)) {
        tbl[idx] = other;
    } else {
        tbl[idx] = obj;
    }
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[2] != 1) return 0;
    const tbl = v.g.D_800F4538();
    const entry = tbl[@as(usize, @intCast(args[0]))];
    return if (entry == null) 1 else 2;
}

pub const stubs = struct {
    pub fn func_8009EFEC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) void {
        _ = c;
        if (b != null) {
            const p: *v.SVECTOR = @ptrCast(@alignCast(b));
            p.vx = ctx.value(i16);
            p.vz = ctx.value(i16);
        }
        // The real callee may replace the table entry the C reads after it.
        if (ctx.value(u2) == 0) {
            v.g.D_800F4538()[@as(usize, @intCast(a))] = obj;
        }
    }

    pub fn func_800AD494(ctx: *zd.Ctx, a: anytype, b: anytype, out: anytype) c_int {
        _ = a;
        _ = b;
        const sel = ctx.value(u8) % 4;
        if (sel == 0) {
            out.* = null;
        } else if (sel == 1) {
            const q: *u16 = @ptrCast(@alignCast(ctx.allocBytes(2, 2)));
            out.* = q;
        } else if (sel == 2) {
            const q: *u16 = @ptrFromInt(0x8000_0010);
            out.* = q;
        } else {
            const q: *u16 = @ptrFromInt(0x10);
            out.* = q;
        }
        return ctx.value(c_int);
    }
};
