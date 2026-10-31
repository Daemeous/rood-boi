//! zig_diff spec: func_80096768 -> func_80096768.func_80096768 (2842C.c).
//! Inputs: the two stat-change flags in vs_main_settings (all four combinations),
//! arg0 over the switch's remapped values (240 and 241 included, plus default
//! values), arg1 at its edges, and arg2 with its low 15 bits around 1000 (the
//! `>= 1000` clamp, both sides EQUAL often) and its sign bit set or clear. The
//! entry list D_800F1BAC[0] points at 16 arena entries whose u8[0] is 6, 7 or
//! other, and D_800F227E is -1..16 (the loop count). The format string is "%d".
//! func_800962E4 hands out an arena block and sometimes rewrites the counter
//! the C increments afterwards.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80096768));

pub const classes = [_][:0]const u8{ "neither", "weapon-240", "armor-not-240", "weapon-only-not-240" };

pub const stubs = struct {
    pub fn func_800962E4(ctx: *zd.Ctx) *v.D_800F1BAC_t {
        // The real allocator may move the entry counter: the C increments it after the call.
        if (ctx.value(u8) < 64) v.g.D_800F227E().* = ctx.value(i16);
        return ctx.alloc(v.D_800F1BAC_t);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const settings = v.g.vs_main_settings();
    psx.setBits(settings, "weaponStatChange", ctx.int(u1));
    psx.setBits(settings, "armorStatChange", ctx.int(u1));

    const arg0: c_int = if (ctx.chance(4))
        240
    else
        ctx.pick(c_int, &.{ 0, 1, 2, 3, 4, 5, 6, 7, 239, 241, 242, -1 });
    const arg1: c_int = ctx.pick(c_int, &.{ 0, 1, 255, 256, -1, 0x7FFF_FFFF, -2147483648 });
    const arg2: c_int = ctx.pick(c_int, &.{
        0,      999,        1000,        1001,        33768,
        32767,  -1,         -2147483648, -2147482648, 0x7FFF_FFFF,
        66536,  -2147483647,
    });

    // The entry list: 16 entries, each with a random u8[0] biased to 6, 7 and others.
    const count = ctx.edge(i16, &.{ -1, 0, 1, 2, 5, 16 });
    v.g.D_800F227E().* = count;
    const raw = ctx.allocBytes(16 * @sizeOf(v.D_800F1BAC_t), 16);
    const entries: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(raw));
    for (0..16) |i| entries[i].unk8.u8[0] = ctx.edge(u8, &.{ 6, 7, 0, 5, 8, 255 });
    v.g.D_800F1BAC().* = @ptrCast(entries);

    // The format sprintf reads: "%d".
    const fmt = v.g.D_800691E4();
    fmt[0] = '%';
    fmt[1] = 'd';
    fmt[2] = 0;

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const settings = v.g.vs_main_settings();
    const w = psx.getBits(settings, "weaponStatChange") != 0;
    const a = psx.getBits(settings, "armorStatChange") != 0;
    const arg0 = args[0];
    if (w and arg0 == 240) return 1;
    if (a and arg0 != 240) return 2;
    if (w) return 3;
    return 0;
}
