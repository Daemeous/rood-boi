//! zig_diff spec: func_800BFD9C -> func_800BFD9C.func_800BFD9C (573B8.c).
//! Inputs: the 17-entry table D_800F4B70 in three shapes: all zero (no calls),
//! a mix of zero, 1, 0x7F, 0x80 and 0xFF bytes (the nonzero and sign-bit
//! cases, with the last entry often set), and all nonzero. Both callees
//! rewrite a random table entry, so the loop sees its own changes as the C does.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BFD9C));

pub const classes = [_][:0]const u8{ "none-set", "some-set", "all-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = v.g.D_800F4B70();
    const mode = ctx.range(u8, 0, 2);
    for (t) |*b| {
        b.* = switch (mode) {
            0 => 0,
            1 => ctx.pick(u8, &.{ 0, 0, 1, 0x7F, 0x80, 0xFF }),
            else => ctx.range(u8, 1, 255),
        };
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    var n: usize = 0;
    for (v.g.D_800F4B70().*) |b| {
        if (b != 0) {
            n += 1;
        }
    }
    return if (n == 0) 0 else if (n == 17) 2 else 1;
}

/// Callees may rewrite one table entry (the C reads the table back in its loop).
fn scribble(ctx: *zd.Ctx) void {
    if (ctx.value(u1) == 1) {
        const t = v.g.D_800F4B70();
        const k = ctx.value(u8) % 17;
        t[k] = ctx.value(u8);
    }
}

pub const stubs = struct {
    pub fn func_8007D260(ctx: *zd.Ctx, i: anytype) void {
        _ = i;
        scribble(ctx);
    }

    pub fn func_800BBDDC(ctx: *zd.Ctx) void {
        scribble(ctx);
    }
};
