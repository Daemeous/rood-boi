//! zig_diff spec: func_800C4650 -> func_800C4650.func_800C4650 (5BE50.c).
//! Inputs: a D_800EB9B8 bank in the arena (the global is set by prepare; a
//! second bank is kept for func_800C1564 to swap in, since the C re-reads the
//! global each iteration), and an array of 0-8 func_800C4650_t entries with
//! type bytes whose high nibble is zero or not (0x08 and 0x0F test the
//! nibble's edge), unkA at 0, -1, 127 and -128, and a count from i32 minimum
//! through -1, 0 and 1-8. func_800C1564 is a stub that rewrites the entry
//! it was passed and the D_800EB9B8 pointer sometimes, and returns zero or
//! a nonzero value; func_8009FD5C and func_8009FE74 are recording stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BE50");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C4650));

pub const classes = [_][:0]const u8{ "no-entries", "skipped-hi-nibble", "checked" };

/// The second D_800EB9B8 bank, swapped in by the func_800C1564 stub.
var other: ?*v.D_800EB9B8_t = null;

pub const stubs = struct {
    pub fn func_800C1564(ctx: *zd.Ctx, arg0: anytype, arg1: anytype) c_int {
        _ = arg1;
        // The real callee may rewrite the entry it was passed (unk2 is read
        // by nothing here, but a port that reads it must agree) ...
        const a: *v.func_800C1564_t = @ptrCast(@alignCast(arg0.?));
        if (ctx.value(u4) == 0) a.unk2 = ctx.value(i16);
        // ... and the D_800EB9B8 global, which the C re-reads every iteration.
        if (ctx.value(u5) == 0) v.g.D_800EB9B8().* = other;
        // The C tests the result for nonzero.
        if (ctx.value(u3) == 0) return 0;
        return ctx.value(c_int) | 1;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const bank = ctx.alloc(v.D_800EB9B8_t);
    other = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = bank;

    const count: c_int = switch (ctx.int(u8) % 8) {
        0 => 0,
        1 => -1,
        2 => std.math.minInt(c_int),
        3 => ctx.range(c_int, -64, -2),
        else => ctx.range(c_int, 1, 8),
    };

    const n: usize = if (count > 0) @intCast(count) else 1;
    const block: [*]v.func_800C4650_t = @ptrCast(@alignCast(ctx.allocBytes(n * @sizeOf(v.func_800C4650_t), 16)));
    for (0..n) |i| {
        block[i].unk9 = ctx.pick(u8, &.{ 0x00, 0x08, 0x0F, 0x10, 0x7F, 0x80, 0xF0, 0xFF });
        block[i].unkA = ctx.pick(i8, &.{ 0, -1, 127, -128 });
    }
    return .{ &block[0], count };
}

pub fn classify(args: Args) usize {
    if (args[1] <= 0) return 0;
    if ((args[0].?.unk9 >> 4) != 0) return 1;
    return 2;
}
