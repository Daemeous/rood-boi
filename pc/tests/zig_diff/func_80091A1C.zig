//! zig_diff spec: func_80091A1C -> func_80091A1C.func_80091A1C (2842C.c).
//! Inputs: two arbitrary c_int ids, the second equal to the first about a
//! quarter of the time (the aliased swap). The callee func_8009195C is a
//! stub that returns NULL about a quarter of the time, otherwise a pointer
//! into a fixed arena slot chosen by the id (so equal ids alias, and ids
//! that agree mod 16 share a slot). The stub also rewrites its slot about
//! half the time, so the C's loads after the calls see changed values.
//! Covered: both NULL tests true and false in every combination, the
//! short-circuit, the swap with and without aliasing, and both returns.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091A1C));

pub const classes = [_][:0]const u8{ "distinct-args", "same-args" };

/// The arena slot an id names: 16 slots of 16 bytes at arena offset 4096.
fn slot(id: anytype) *c_int {
    const k: u32 = @bitCast(@as(c_int, id));
    const off: usize = 4096 + (@as(usize, k) % 16) * 16;
    return @ptrCast(@alignCast(&zd.zdiff_arena[off]));
}

pub const stubs = struct {
    pub fn func_8009195C(ctx: *zd.Ctx, id: anytype) ?*c_int {
        if (ctx.value(u8) < 64) return null; // the C's NULL test
        const p = slot(id);
        if (ctx.value(u1) == 1) p.* = ctx.value(c_int); // a value the C re-reads
        return p;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.pick(c_int, &.{ 0, 1, -1, 15, 16, 0x7fffffff, -2147483648 });
    const b = if (ctx.chance(4)) a else ctx.pick(c_int, &.{ 0, 1, -1, 16, 17, 0x7fffffff });
    return .{ a, b };
}

pub fn classify(args: Args) usize {
    return if (args[0] == args[1]) 1 else 0;
}
