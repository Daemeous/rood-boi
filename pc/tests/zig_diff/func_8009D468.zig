//! zig_diff spec: func_8009D468 -> func_8009D468.func_8009D468 (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 (0 takes the var_a2 = 1 branch,
//! the others 0x62; 16 is the last slot), the entry null or a random actor
//! from the arena, the D_800E8F2C selector 0..7 with its D_800F2450 flag
//! zero, set with the sign bit (0x80, 0xFF) or set without it (0x01, 0x40).
//! The callees write what the C reads back: func_800A07FC sets unk17FD to 1
//! about a third of the time (the `== 1` branch), func_800A0204 rewrites the
//! shadow rates the C copies, func_8009FD38 rewrites the flag the C tests
//! after it, func_8009F990 may fill the scratch struct. Each stub returns
//! the prototype's own return type, read from the c module. arg1 is random
//! and unused by the C.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_30DB0");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D468));

pub const classes = [_][:0]const u8{ "null-entry", "flag-zero", "flag-set", "flag-set-arg0-zero" };

fn RetOf(comptime name: []const u8) type {
    return @typeInfo(@TypeOf(@field(c, name))).@"fn".return_type.?;
}

fn result(ctx: *zd.Ctx, comptime R: type) R {
    return if (R == void) {} else ctx.value(R);
}

pub const stubs = struct {
    pub fn func_8009FD38(ctx: *zd.Ctx, p: anytype) void {
        _ = p;
        const sel: usize = v.g.D_800E8F2C().*;
        if (ctx.value(u8) < 128) v.g.D_800F2450()[sel] = ctx.value(u8);
    }

    pub fn func_800AB098(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) RetOf("func_800AB098") {
        _ = p;
        _ = a;
        _ = b;
        return result(ctx, RetOf("func_800AB098"));
    }

    pub fn func_8009F990(ctx: *zd.Ctx, a: anytype, p: anytype) RetOf("func_8009F990") {
        _ = a;
        const out: *v.D_800F4538_unk64 = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) out.unk0 = ctx.value(u8);
        return result(ctx, RetOf("func_8009F990"));
    }

    pub fn func_800A07FC(ctx: *zd.Ctx, a0: anytype, p: anytype) RetOf("func_800A07FC") {
        _ = p;
        const t = v.g.D_800F4538().*[@intCast(a0)].?;
        if (ctx.value(u8) < 96) t.unk17FD = 1;
        return result(ctx, RetOf("func_800A07FC"));
    }

    pub fn func_800A0204(ctx: *zd.Ctx, a0: anytype, idx: anytype, a: anytype, b: anytype) RetOf("func_800A0204") {
        _ = idx;
        _ = a;
        _ = b;
        const t = v.g.D_800F4538().*[@intCast(a0)].?;
        t.shadowRadiusExpandRate = ctx.value(u16);
        t.shadowRadiusShrinkRate = ctx.value(u16);
        return result(ctx, RetOf("func_800A0204"));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: c_int = ctx.edge(c_int, &.{ 0, 0, 1, 2, 9, 16 });
    const i: usize = @intCast(idx);
    const table = v.g.D_800F4538();
    if (ctx.chance(8)) {
        table[i] = null;
    } else {
        const t = ctx.alloc(v.D_800F4538_t);
        table[i] = t;
    }
    v.g.D_800E8F2C().* = ctx.edge(u8, &.{ 0, 1, 3, 7 });
    const sel: usize = v.g.D_800E8F2C().*;
    v.g.D_800F2450()[sel] = ctx.edge(u8, &.{ 0, 0x80, 0xFF, 1, 0x40 });
    return .{ idx, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const i: usize = @intCast(args[0]);
    if (v.g.D_800F4538().*[i] == null) return 0;
    const flag = v.g.D_800F2450()[v.g.D_800E8F2C().*];
    if (flag == 0) return 1;
    return if (args[0] == 0) 3 else 2;
}
