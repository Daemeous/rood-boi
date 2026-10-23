//! zig_diff spec: func_800DC0D0 -> func_800DC0D0.func_800DC0D0 (73700.c).
//! Inputs: the global D_800F58BC (always valid, since the C dereferences it)
//! points at a random block whose unk1C is either null or the head of a
//! chain of 0-4 actorTimerState nodes. Every node has a valid actor flags
//! block (the C dereferences it unconditionally) and a next link that is
//! null or the next node. targetFlags covers the skip case (>>6 == 2) and
//! the non-skip cases. phase covers 0, 16, 15, 17, 127 and -128 (the i8
//! increment wraps), and direction covers both signs and zero. The C makes
//! no external calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_73700");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DC0D0));

pub const classes = [_][:0]const u8{ "head-null", "first-skipped", "phase-0", "phase-16", "phase-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = d;

    // Build the chain back to front so the last node built is the head.
    const count = ctx.range(u8, 0, 4);
    var cur: ?*v.actorTimerState = null;
    var i: u8 = 0;
    while (i < count) : (i += 1) {
        const node = ctx.alloc(v.actorTimerState);
        const act = ctx.alloc(v.actorPhaseFlags);
        psx.put(&node.actor, act);
        psx.put(&node.next, cur);
        node.targetFlags = ctx.pick(u8, &.{ 0x80, 0xC0, 0x40, 0x00, 0x7F, 0xBF });
        node.phase = ctx.pick(i8, &.{ 0, 16, 1, 15, 17, 127, -128, -1 });
        node.direction = ctx.pick(i8, &.{ 1, -1, 0, 127, -128 });
        cur = node;
    }

    if (ctx.chance(6)) {
        psx.put(&d.unk1C, null);
    } else {
        psx.put(&d.unk1C, cur);
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const d = v.g.D_800F58BC().*.?;
    const head: *v.actorTimerState = @ptrCast(@alignCast(psx.get(d.unk1C) orelse return 0));
    if (head.targetFlags >> 6 == 2) return 1;
    if (head.phase == 0) return 2;
    if (head.phase == 16) return 3;
    return 4;
}
