//! zig_diff spec: func_800E2B2C -> func_800E2B2C.func_800E2B2C (771A8.c).
//! Inputs: a movementDecisionContext in the arena whose actor pointer is an
//! arena actor with random facing and unk14 (the C's `(short)` cast included),
//! a target index 0..16 that equals actorId about one time in five, a delay
//! that is zero about one time in five, a reaction that is zero or has its sign
//! bit set, a pending byte that is zero or not, destinations that match under
//! the 0xFF00FF mask or differ in it, target flags with bit 5 set or clear, and
//! an angle equal to (facing + unk14) & 0xFFF, the same with bit 11 flipped, or
//! random. The three callees are recording stubs: ratan2 returns any c_int,
//! func_800DEEFC may rewrite timer, and func_800E4180 returns zero or nonzero
//! and may rewrite destination.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E2B2C));

pub const classes = [_][:0]const u8{ "same-actor", "no-delay", "reaction", "arm", "idle", "aligned", "turn" };

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        return ctx.value(c_int);
    }

    pub fn func_800DEEFC(ctx: *zd.Ctx, context: anytype, mode: anytype) void {
        _ = mode;
        const c: *v.movementDecisionContext = @ptrCast(@alignCast(context));
        if (ctx.value(u1) == 1) c.timer = ctx.value(u16);
    }

    pub fn func_800E4180(ctx: *zd.Ctx, context: anytype, a: anytype, b: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = d;
        const c: *v.movementDecisionContext = @ptrCast(@alignCast(context.?));
        if (ctx.value(u1) == 1) c.destination = ctx.value(i32);
        // the C's zero test: zero about half the time, nonzero otherwise
        if (ctx.value(u8) < 128) return 0;
        return ctx.value(c_int) | 1;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const context = ctx.alloc(v.movementDecisionContext);
    const actor = ctx.alloc(v.D_800F4538_t);
    psx.put(&context.actor, actor);

    const target = ctx.range(c_int, 0, 16);
    const idx: usize = @intCast(target);
    if (ctx.chance(5)) context.actorId = @intCast(target);

    actor.unk0.facing = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    actor.unk0.unk14 = ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0x8000, 0x0FFF });

    context.delay = if (ctx.chance(5)) 0 else ctx.pick(u16, &.{ 1, 2, 0xFFFF, 0x8000 });
    context.reaction = if (ctx.chance(4)) 0 else ctx.pick(u8, &.{ 1, 127, 128, 255 });
    context.pending = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });

    context.destination = ctx.int(i32);
    context.previousDestination = context.destination ^ ctx.pick(i32, &.{ 0, 0x100, 0x10000, 0x1000, 0x01000000 });

    context.targetFlags[idx] = if (ctx.chance(2))
        ctx.int(u32) | (1 << 5)
    else
        ctx.int(u32) & ~@as(u32, 1 << 5);

    const sum: c_int = @as(c_int, actor.unk0.facing) + @as(c_int, @as(i16, @bitCast(actor.unk0.unk14)));
    var want: i16 = @truncate(sum & 0xFFF);
    if (ctx.chance(4)) want ^= 0x800; // differs from the target in bit 11 only
    if (ctx.chance(3)) {
        psx.setBits(context, "angle", ctx.int(i16));
    } else {
        psx.setBits(context, "angle", want);
    }

    return .{ context, target };
}

pub fn classify(args: Args) usize {
    const context = args[0].?;
    const target = args[1];
    if (target == @as(c_int, context.actorId)) return 0;
    if (context.delay == 0) return 1;
    if (context.reaction != 0) return 2;
    const flagged = ((context.targetFlags[@intCast(target)] >> 5) & 1) == 1;
    const moved = (context.destination & 0xFF00FF) != (context.previousDestination & 0xFF00FF);
    if (context.pending == 0 and moved and flagged) return 3;
    if (context.pending == 0) return 4;
    const actor = psx.get(context.actor).?;
    const facing: c_int = @as(c_int, actor.unk0.facing) + @as(c_int, @as(i16, @bitCast(actor.unk0.unk14)));
    const angle: c_int = psx.getBits(context, "angle");
    return if ((facing & 0xFFF) == (angle & 0xFFF)) 5 else 6;
}
