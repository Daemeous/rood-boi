//! zig_diff spec: func_800A2790 -> func_800A2790.func_800A2790 (38C1C.c).
//! Inputs: one actor in the arena, always valid (the C never null-checks it).
//! The speed (unk1848.unk8, read as a signed int) covers 0 and 12 exactly,
//! negative values (sign bit set), and speeds in 2000..4000 whose per-frame
//! delta.vy lands on -3..3, so `delta.vy < -2` and `delta.vy >= 3` both see
//! their right-hand side equal. Velocity and position are edge-biased
//! (positions at i16 limits so position + delta wraps, negative positions so
//! the signed /128 tile differs from a shift). The animation id is set to the
//! expected value half the time, so the `!=` test takes both sides. Every
//! value stays inside int range, so the C never hits signed overflow.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A2790));

pub const classes = [_][:0]const u8{
    "animation-unchanged",
    "not-moving",
    "rising",
    "falling",
    "level-slow",
    "level-fast",
};

const edge_speeds = [_]u32{ 0, 1, 11, 12, 13 };
const big_speeds = [_]u32{ 0xFFFF_FFFF, 0x8000_0000, 0x7FFF_FFFF };

/// delta.vy = vy * speed / 0x1000000 as the C computes it (no overflow here).
fn deltaY(vy: i16, speed: i32) i32 {
    return @intCast(@divTrunc(@as(i64, vy) * @as(i64, speed), 0x1000000));
}

/// The animation the C picks for this speed and vertical delta.
fn expectedAnim(speed: i32, dvy: i32) u8 {
    if (speed <= 0) return 1;
    var anim: u8 = if (dvy < -2) 32 else if (dvy >= 3) 34 else 31;
    if (speed >= 12) anim += 6;
    return anim;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);

    var speed: u32 = undefined;
    var vy: i16 = undefined;
    const r = ctx.random.uintLessThan(u8, 8);
    switch (r) {
        0, 1 => {
            // Speeds on the 0 and 12 edges, small velocity.
            speed = edge_speeds[ctx.random.uintLessThan(usize, edge_speeds.len)];
            vy = ctx.range(i16, -200, 200);
        },
        2 => {
            // Negative or huge speeds: vy stays 0 or 1 so the product fits an int.
            speed = big_speeds[ctx.random.uintLessThan(usize, big_speeds.len)];
            vy = ctx.range(i16, 0, 1);
        },
        3, 4, 5 => {
            // A target per-frame delta.vy in -3..3, placed mid-band so the
            // truncating division lands exactly on it.
            speed = ctx.range(u32, 2000, 4000);
            const d: i32 = ctx.range(i32, -3, 3);
            const sgn: i32 = if (d >= 0) 1 else -1;
            vy = @intCast(@divTrunc((2 * d + sgn) * (1 << 23), @as(i32, @intCast(speed))));
        },
        else => {
            speed = ctx.range(u32, 0, 4000);
            vy = ctx.int(i16);
        },
    }

    actor.unk1848.unk8 = speed;
    actor.unk1848.unk10.vx = ctx.int(i16);
    actor.unk1848.unk10.vy = vy;
    actor.unk1848.unk10.vz = ctx.int(i16);

    actor.unk0.position.vx = ctx.pick(i16, &.{ 32767, -32768, 0, 127, -128, 128, -129 });
    actor.unk0.position.vy = ctx.int(i16);
    actor.unk0.position.vz = ctx.pick(i16, &.{ 32767, -32768, 0, 127, -128, 128, -129 });

    const sp: i32 = @bitCast(speed);
    const dvy = deltaY(vy, sp);
    actor.animationId = if (ctx.chance(2)) expectedAnim(sp, dvy) else ctx.int(u8);

    return .{actor};
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    const sp: i32 = @bitCast(actor.unk1848.unk8);
    const dvy = deltaY(actor.unk1848.unk10.vy, sp);
    if (actor.animationId == expectedAnim(sp, dvy)) return 0;
    if (sp <= 0) return 1;
    if (dvy < -2) return 2;
    if (dvy >= 3) return 3;
    return if (sp >= 12) 5 else 4;
}
