//! zig_diff spec: func_8009FC60 -> func_8009FC60.func_8009FC60 (30DB0.c).
//! Inputs: a table index 0..16 (the ends included) whose entry is an actor in
//! the arena or null (the other entries are null, so the C never reads
//! garbage), arg1 around the i8 truncation edges, arg2 null, an arena SVECTOR,
//! or an alias of the actor's own unk17F4 (a self-copy), and arg3 -1 (the
//! callee path) or a pad value that truncates to i16 (0, 0x8000, 0xFFFF,
//! 0x10000). func_800A152C returns negative, exactly 0, and any c_int; it can
//! write the actor's unk1800. func_8009FD38 can write the actor's pad and
//! unk17FC.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FC60));

pub const classes = [_][:0]const u8{ "no-object", "fields-pad", "fields-callee", "src-pad", "src-callee" };

var actor: ?*v.D_800F4538_t = null;

pub const stubs = struct {
    pub fn func_800A152C(ctx: *zd.Ctx, actorId: anytype, a: anytype, b: anytype) c_int {
        _ = actorId;
        _ = a;
        _ = b;
        const obj = actor.?;
        if (ctx.value(u2) == 0) obj.unk1800 = ctx.value(i16);
        const k = ctx.value(u8);
        if (k < 80) return -1 - @as(c_int, ctx.value(u8));
        if (k < 128) return 0;
        return ctx.value(c_int);
    }

    pub fn func_8009FD38(ctx: *zd.Ctx, p: anytype) void {
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        if (ctx.value(u2) == 0) a.unk17F4.pad = ctx.value(i16);
        if (ctx.value(u2) == 0) a.unk17FC = ctx.value(i8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const tbl = v.g.D_800F4538();
    for (tbl) |*e| e.* = null;

    const arg0: c_int = if (ctx.chance(2)) ctx.edge(c_int, &.{ 0, 16 }) else ctx.range(c_int, 0, 16);
    const idx: usize = @intCast(arg0);

    const obj = ctx.alloc(v.D_800F4538_t);
    tbl[idx] = if (ctx.chance(8)) null else obj;
    actor = obj;

    const arg1: c_int = ctx.pick(c_int, &.{ 0, 127, 128, -1, -128, 256 });
    const src: ?*v.SVECTOR = if (ctx.chance(3))
        null
    else if (ctx.chance(6))
        &obj.unk17F4
    else
        ctx.alloc(v.SVECTOR);
    const arg3: c_int = if (ctx.chance(2)) -1 else ctx.pick(c_int, &.{ 0, 1, -2, 0xFFFF, 0x10000, 0x8000 });

    return .{ arg0, arg1, src, arg3 };
}

pub fn classify(args: Args) usize {
    const tbl = v.g.D_800F4538();
    const idx: usize = @intCast(args[0]);
    if (tbl[idx] == null) return 0;
    const src = args[2] != null;
    const callee = args[3] == -1;
    return 1 + @as(usize, @intFromBool(src)) * 2 + @as(usize, @intFromBool(callee));
}
