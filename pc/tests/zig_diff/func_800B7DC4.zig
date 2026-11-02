//! zig_diff spec: func_800B7DC4 -> func_800B7DC4.func_800B7DC4 (4A0A8.c).
//! Inputs: an 8-byte record in the arena whose bytes 3, 4 and 5 are the
//! edge values the C tests: arg0[4] is 0 (the else branch), -1, 1, or another
//! value with and without the sign bit; arg0[5] is 0xFF or not, and a signed
//! read differs from an unsigned one (0x80, 0x90, 0xFE). The pad that
//! func_800A1108 writes back is chosen so that var_a1 hits the comparison
//! edges (0, -2048, 2048 and their neighbours) often, and is random otherwise,
//! so the wrap-around branches and the case -1 / case 1 tests are all taken.
//! The temp index comes from func_800BFE50 and is kept inside the 24-byte
//! D_800F4B70 table, a third of the time with bits above 16 set that the
//! C's u_short drops.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7DC4));

pub const classes = [_][:0]const u8{ "selector-zero", "selector-minus-one", "selector-one", "selector-other" };

/// The pad func_800A1108 writes back; set by prepare, read by the stub.
var pad_target: i16 = 0;

pub const stubs = struct {
    /// Returns the table index, kept inside the 24-entry D_800F4B70 once narrowed.
    pub fn func_800BFE50(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        const actor: c_int = ctx.value(u8) % 24;
        return if (ctx.value(u8) % 3 == 0) actor | (@as(c_int, ctx.value(u8)) << 16) else actor;
    }

    /// Writes the pad the C reads back: the edge chosen by prepare most of
    /// the time, a random value otherwise.
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const raw: ?*anyopaque = out;
        const p: *v.func_8006EBF8_t = @ptrCast(@alignCast(raw.?));
        p.unk0.unk4.pad = if (ctx.value(u8) < 200) pad_target else ctx.value(i16);
        return 0;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const b = ctx.allocBytes(8, 16);
    b[1] = ctx.int(u8);
    b[2] = ctx.int(u8);
    b[3] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    b[4] = ctx.edge(u8, &.{ 0, 1, 0xFF, 2, 0x80, 0x7F, 0xFE });
    b[5] = ctx.edge(u8, &.{ 0, 0xFF, 0xFE, 1, 0x80, 0x90, 0x7F });
    const base: i32 = @as(i32, b[3]) << 4;
    // var_a1 = base - pad, so pad makes var_a1 equal one of these.
    const want = ctx.edge(i32, &.{ 0, 0, 2048, -2048, -2049, 2049, 4096, -4096, 1, -1 });
    pad_target = @intCast(base - want);
    return .{ b, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return switch (args[0][4]) {
        0 => 0,
        0xFF => 1,
        1 => 2,
        else => 3,
    };
}
