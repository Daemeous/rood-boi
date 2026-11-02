//! zig_diff spec: func_800BE180 -> func_800BE180.func_800BE180 (4A0A8.c).
//! Inputs: a D_800F4BA4 object in the arena, set through its global pointer,
//! with its two camera-shake entries at unk1A4 (entry i at 0x1A6 + 12*i, as the
//! C addresses them). Each entry gets a random unk2 (edges 0, +-1, 100 and the
//! i16 extremes, so the products wrap on the store to the short vectors) and a
//! random unkA (every 2-bit amplitude and flag combination). func_800BE01C is a
//! recording stub that returns zero or nonzero and sometimes rewrites the
//! entry's unkA and unk2. The scratchpad is random, so the cleared a/b/c
//! vectors and the a/b vectors read back are compared too. `count` is never
//! negative (at most 2), so its zero test has no sign-bit case to reach.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BE180));

pub const classes = [_][:0]const u8{ "flags-0", "flags-1", "flags-2", "flags-3" };

fn entryAt(d: *v.D_800F4BA4_t, i: usize) *align(1) v.func_800BDF6C_t {
    const off: isize = @intCast(0x1A6 + 12 * i);
    return psx.byteAt(v.func_800BDF6C_t, d, off);
}

pub const stubs = struct {
    pub fn func_800BE01C(ctx: *zd.Ctx, entry: anytype) c_int {
        const e: *align(1) v.func_800BDF6C_t = @ptrCast(entry);
        const hit = ctx.value(u2) != 0;
        if (ctx.value(u1) == 1) e.unkA = ctx.value(u8);
        if (ctx.value(u1) == 1) e.unk2 = ctx.value(i16);
        if (!hit) return 0;
        return ctx.value(i32) | 1;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = d;
    for (0..2) |i| {
        const e = entryAt(d, i);
        e.unk2 = ctx.pick(i16, &.{ 0, 1, -1, 100, 32767, -32768 });
        e.unkA = ctx.pick(u8, &.{ 0, 1, 2, 3, 0x40, 0x80, 0xFF });
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const d = v.g.D_800F4BA4().*.?;
    return @as(usize, entryAt(d, 0).unkA & 3);
}
