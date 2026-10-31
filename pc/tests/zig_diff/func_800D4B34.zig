//! zig_diff spec: func_800D4B34 -> func_800D4B34.func_800D4B34 (5BF94.c).
//! Inputs: a D_800F53B8_t from the arena with random bytes. The index
//! func_800D5170 returns is 0..3 (inside unk10) or 4..255 (past unk10, into
//! the rest of the object the arena holds). func_800D5198 returns the byte at
//! that index half the time and any u16 otherwise, so the == is true and
//! false alike. The C has no branch of its own; both callees are recording
//! stubs, and the stub for func_800D5198 reads unk10 through a byte pointer
//! so an index past the array does not trip a Zig bounds check.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4B34));

pub const classes = [_][:0]const u8{ "index-in-unk10", "index-past-unk10" };

/// The index func_800D5170 returns in this iteration, set by prepare.
var chosen: u8 = 0;

pub const stubs = struct {
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        _ = ctx;
        _ = p;
        return chosen;
    }

    pub fn func_800D5198(ctx: *zd.Ctx, p: anytype) u16 {
        const a: *v.D_800F53B8_t = @ptrCast(@alignCast(p.?));
        const base: [*]u8 = @ptrCast(&a.unk10);
        if (ctx.value(u1) == 1) return base[chosen];
        return ctx.value(u16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F53B8_t);
    chosen = ctx.pick(u8, &.{ 0, 3, 4, 255 });
    return .{obj};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (chosen < 4) 0 else 1;
}
