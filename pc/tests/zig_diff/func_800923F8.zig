//! zig_diff spec: func_800923F8 -> func_800923F8.func_800923F8 (2842C.c).
//! Inputs: a D_800F1DD8_t2 in the arena (its restart flag 0 or 1 and 0x80/0xFF
//! among the nonzero values, selector 1, 2 or other, shift 0-31, velocity,
//! acceleration and targets) whose geometry pointer is set by prepare to a
//! 256-byte arena block, with the write offset unk0 inside it. Half the
//! runs set up the clamp case: the velocity is chosen so that its shifted
//! magnitude is 31, 32, 33 or 34 (or 0), so `ABS(unk8) < 0x21` sees both
//! sides equal and the damped result lands on both sides of 33. The target
//! is placed so that the distance `delta` equals |velocity| or 2048, 2047,
//! 2049, 0 or 4095, which makes the `delta` comparisons equal.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800923F8));

pub const classes = [_][:0]const u8{ "restart", "sel1", "sel2", "sel-other" };

fn absU(x: i32) u32 {
    return if (x < 0) @as(u32, @bitCast(0 -% x)) else @as(u32, @intCast(x));
}

fn bounded(ctx: *zd.Ctx) i32 {
    if (ctx.chance(3)) return ctx.edge(i32, &.{ 0, 1, -1, 2, 32, 33, -33, 0x21 << 4, -(0x21 << 4) });
    return ctx.range(i32, -(1 << 29), 1 << 29);
}

fn wide(ctx: *zd.Ctx) i32 {
    return ctx.pick(i32, &.{ 0, 1, -1, std.math.maxInt(i32), std.math.minInt(i32), 4096, -4096 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(256, 16);
    const base: *i32 = @ptrCast(@alignCast(block + 128));
    const room = v.g.vs_battle_roomData();
    psx.put(&room.geometrySection, base);

    const d = ctx.alloc(v.D_800F1DD8_t2);
    d.unk0 = 4 * ctx.range(i32, -32, 24);
    d.unk4 = if (ctx.chance(2)) 0 else ctx.edge(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    const restart = d.unk4 != 0;
    d.unk7 = ctx.edge(u8, &.{ 1, 2, 0, 3, 255 });
    d.unk8 = bounded(ctx);
    d.unkC = if (ctx.chance(3)) 0 else ctx.range(i32, -(1 << 26), 1 << 26);
    d.unk10 = ctx.range(i32, 0, 31);
    d.unk14 = wide(ctx);
    d.unk1C = wide(ctx);
    d.unk20 = bounded(ctx);

    // The clamp setup: the velocity's shifted magnitude is m (31..34 or 0).
    if (ctx.chance(3)) {
        const s: u5 = ctx.range(u5, 0, 6);
        const m = ctx.edge(i32, &.{ 0, 31, 32, 33, 34 });
        const ofs = ctx.range(i32, 0, (@as(i32, 1) << s) - 1);
        const sign: i32 = if (ctx.chance(2)) 1 else -1;
        const src = sign * (m << s) + ofs;
        d.unk10 = @as(i32, s);
        d.unkC = 0;
        if (restart) d.unk20 = src else d.unk8 = src;
    }

    const s14: i32 = if (restart) d.unk1C else d.unk14;
    const s8: i32 = if (restart) d.unk20 else d.unk8;
    const pre: i32 = s8 +% d.unkC *% 8;
    const after: i32 = s14 +% s8;

    const dist: i32 = switch (ctx.range(u8, 0, 3)) {
        0 => ctx.range(i32, 0, 4096),
        1 => @intCast(@min(absU(pre), 2048)),
        else => ctx.edge(i32, &.{ 2048, 2047, 2049, 0, 4095, 1, 2050 }),
    };
    d.unk18 = after -% dist;

    return .{d};
}

pub fn classify(args: Args) usize {
    const d = args[0].?;
    if (d.unk4 != 0) return 0;
    return switch (d.unk7) {
        1 => 1,
        2 => 2,
        else => 3,
    };
}
