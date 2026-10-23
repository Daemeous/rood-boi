//! zig_diff spec: func_8009121C -> func_8009121C.func_8009121C (2842C.c).
//! Inputs: the battle globals DBC (sfx id, any i16, sign and zero included),
//! DC4 (a table index, in 0..79 whenever the C reads the table, any i16 when
//! it does not), DC2 (clamp bound) and the 80-byte table D_80068EB4 with zero
//! and nonzero entries (both sign-bit cases). The playSfx stub rewrites DC4
//! (in range) and DBC, so a port that caches either across the call differs.
//! The clamp return is any c_int, so the narrowing to i16 wraps both ways.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
const g = v.g;

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009121C));

pub const classes = [_][:0]const u8{ "no-table-read", "entry-zero", "entry-nonzero" };

pub const stubs = struct {
    pub fn vs_main_playSfxDefault(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        // The C reads DC4 again for the table index after this call.
        if (ctx.value(u8) < 96) g.D_800F1DC4().* = @intCast(ctx.value(u8) % 80);
        // The C reads DBC again as the next call's argument.
        if (ctx.value(u8) < 32) g.D_800F1DBC().* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const dbc = g.D_800F1DBC();
    const dc4 = g.D_800F1DC4();
    g.D_800F1DC2().* = ctx.pick(i16, &.{ 0, 4, 12, 90, -1, 32767, -32768 });
    dbc.* = ctx.pick(i16, &.{ 0, 0, 1, -1, 32767, -32768 });
    if (dbc.* >= 0) {
        dc4.* = if (ctx.chance(4)) ctx.edge(i16, &.{ 0, 79 }) else ctx.range(i16, 0, 79);
        const tbl = g.D_80068EB4();
        tbl[@intCast(dc4.*)] = if (ctx.chance(3)) 0 else ctx.int(u8);
    } else {
        dc4.* = ctx.pick(i16, &.{ -1, -32768, 32767, 0, 80 });
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const dbc = g.D_800F1DBC().*;
    if (dbc < 0) return 0;
    const dc4 = g.D_800F1DC4().*;
    const tbl = g.D_80068EB4();
    return if (tbl[@intCast(dc4)] != 0) 2 else 1;
}
