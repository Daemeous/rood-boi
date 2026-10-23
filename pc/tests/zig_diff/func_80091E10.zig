//! zig_diff spec: func_80091E10 -> func_80091E10.func_80091E10 (2842C.c).
//! Inputs: a random func_80091E10_t in the arena, its tick just below its delay
//! (so the increment compares equal), at the delay, or wrapping from 255 to 0;
//! its window, position and step fields around the u8 and i8 edges, with steps
//! of 0, -1 and the extremes, and u and v that wrap or go negative. The room's
//! geometry section is a 1 KB arena block; the stepped polygon lies in it at a
//! signed offset from the base, and its opcode byte at 0xF is 0x34 (the first
//! branch) or one of the other values the C tests (the else branch). The C
//! calls nothing, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091E10));

pub const classes = [_][:0]const u8{ "holds", "fires-steady", "fires-moving" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const q = ctx.alloc(v.func_80091E10_t);
    const room = v.g.vs_battle_roomData();
    const block = ctx.allocBytes(1024, 16);
    psx.put(&room.geometrySection, block + 256);

    q.delay = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    q.tick = if (ctx.chance(2)) q.delay -% 1 else ctx.edge(u8, &.{ 0, 254, 255 });

    q.geometryOffset = ctx.range(i32, -200, 500);
    const poff: usize = @intCast(256 + q.geometryOffset);
    const poly = block + poff;
    poly[0xF] = if (ctx.chance(2))
        (ctx.int(u8) & 0xC3) | 0x34
    else
        ctx.edge(u8, &.{ 0x00, 0x24, 0x38, 0x3C, 0xFF });

    q.baseU = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    q.baseV = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    q.width = ctx.pick(u8, &.{ 0, 1, 8, 64, 255 });
    q.height = ctx.pick(u8, &.{ 0, 1, 8, 64, 255 });
    q.windowWidth = ctx.pick(u8, &.{ 0, 1, 8, 64, 255 });
    q.windowHeight = ctx.pick(u8, &.{ 0, 1, 8, 64, 255 });
    q.currentU = if (ctx.chance(3)) q.baseU else ctx.int(u8);
    q.currentV = if (ctx.chance(3)) q.baseV else ctx.int(u8);
    q.stepU = ctx.pick(i8, &.{ 0, 1, -1, 127, -128 });
    q.stepV = ctx.pick(i8, &.{ 0, 1, -1, 127, -128 });

    return .{q};
}

pub fn classify(args: Args) usize {
    const q = args[0].?;
    if (q.tick +% 1 <= q.delay) return 0;
    return if (q.stepU == 0 and q.stepV == 0) 1 else 2;
}
