//! zig_diff spec: func_800D48A4 -> func_800D48A4.func_800D48A4 (5BF94.c).
//! Inputs: one D_800F53B8_t in the arena with random bytes (every field the
//! callees or the C touch is random). The class is whether unkA already holds
//! 0xB7, so the final store is or is not visible in the state. func_800D5170
//! returns any u8, with the high bit set half the time, so a port that masks
//! differently or passes the wrong value is caught. func_800D55A4 writes unkA
//! and unk8, so a port that stores 0xB7 before the call (or drops the call)
//! diverges in the state or the call trace.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D48A4));

pub const classes = [_][:0]const u8{ "other-unkA", "already-B7" };

pub const stubs = struct {
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        const d: *v.D_800F53B8_t = @ptrCast(@alignCast(p.?));
        d.unk9 = ctx.value(u8);
        return ctx.value(u8);
    }

    pub fn func_800D55A4(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        const d: *v.D_800F53B8_t = @ptrCast(@alignCast(p.?));
        d.unkA = ctx.value(u16);
        d.unk8 = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F53B8_t);
    if (ctx.chance(3)) obj.unkA = 0xB7;
    return .{obj};
}

pub fn classify(args: Args) usize {
    return if (args[0].?.unkA == 0xB7) 1 else 0;
}
