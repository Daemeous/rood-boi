//! zig_diff spec: func_800A30A0 -> func_800A30A0.func_800A30A0 (3A1A0.c).
//! Inputs: an actor id 0-16 whose D_800F4538 entry is an arena actor, a random
//! placement block whose tile type is an edge (0, 1, 2, 6, 7, 8, 0x87, 255) so
//! the tile-7 and tile >= 2 branches and their equal sides are hit, a height
//! with 16-bit edges, and a mode from both branches (0x10/0x20, 0x40/0x80 and
//! the rest). The ceiling callee derives its answer from the actor's position.y
//! so that value == -0x7F is hit often, and returns 0 (the no-ceiling case) in
//! some runs. The callees are recording stubs: they return spec-chosen values
//! or ctx.value draws, and they write the fields a real callee could change.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A30A0));

pub const classes = [_][:0]const u8{
    "modeA-no-ceiling",
    "modeA-ceiling",
    "tile7",
    "tile-ge2",
    "tile-lt2",
};

// Values the stubs hand back, set by prepare for each run.
var g_actor: *v.D_800F4538_t = undefined;
var g_anchor: *[2]c_int = undefined;
var g_noCeil: bool = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const id: usize = ctx.range(usize, 0, 16);
    const actorId: c_int = @intCast(id);

    g_actor = ctx.alloc(v.D_800F4538_t);
    v.g.D_800F4538()[id] = g_actor;

    const placement = ctx.alloc(v.func_80089888_t);
    placement.unk0_8 = ctx.pick(u8, &.{ 0, 1, 2, 6, 7, 8, 0x87, 255 });

    const modes = [_]c_int{ 0x10, 0x20, 0x40, 0x80, 0, 0x11, 0x30, 0x10 };
    const mode = modes[ctx.range(usize, 0, modes.len - 1)];

    const height: c_int = ctx.pick(c_int, &.{ 0, -1, 0x7FFF, -0x8000, 0x10000, 0x40 });

    g_noCeil = ctx.chance(4);
    g_anchor = ctx.alloc([2]c_int);

    return .{ actorId, placement, height, mode };
}

pub fn classify(args: Args) usize {
    const mode = args[3];
    const tile = args[1].?.unk0_8;
    if (mode == 0x10 or mode == 0x20) return if (g_noCeil) 0 else 1;
    if (tile == 7) return 2;
    return if (tile >= 2) 3 else 4;
}

pub const stubs = struct {
    // The ceiling: none sometimes (0), else the position.y the C just stored
    // shifted so that ceiling - vy is -127 or -128 often.
    pub fn func_800E75EC(ctx: *zd.Ctx) c_int {
        if (g_noCeil) return 0;
        const vy: c_int = g_actor.unk0.position.vy;
        return switch (ctx.value(u3)) {
            0...3 => vy - 127,
            4 => vy - 128,
            else => ctx.value(c_int),
        };
    }

    // Returns 0 or nonzero; may move the position it is given.
    pub fn func_800A3310(ctx: *zd.Ctx, actorId: anytype, position: anytype) c_int {
        _ = actorId;
        const p: *v.SVECTOR = @ptrCast(@alignCast(position));
        if (ctx.value(u1) == 1) p.vx = ctx.value(i16);
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800ACF54(ctx: *zd.Ctx, actor: anytype) void {
        _ = actor;
        g_actor.unk0.unk3C = ctx.value(i16);
    }

    // Null on about a quarter of the calls (the ROOD_PC guard), else the anchor.
    pub fn func_800A4A24(ctx: *zd.Ctx, tile: anytype) ?*anyopaque {
        _ = tile;
        if (ctx.value(u2) == 0) return null;
        return g_anchor;
    }
};
