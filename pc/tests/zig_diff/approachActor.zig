//! zig_diff spec: func_800DB370 -> pc.approach_actor.approachActor (D2: pc/src/battle_movement.c).
//! Inputs: a state block (busy bits 0x38 of the halfword 0x15E, byte 0x29, the
//! last approached id at 0xE5 equal to the id or not); an actor id 0..16 with
//! its scratchpad slot (0x37C + id * 4) zero or set; the model table
//! D_800F4538 with models whose flag bytes (0xA & 0x1F, 0xC & 0xF) and
//! waypoint word (0x60) select every way of picking the target tile and
//! group. The stubs of func_8008E2D4 (a waypoint record), func_800DFA54 (writes
//! a tile through its out pointer), func_800E24EC (the plan) and func_800DB4AC
//! answer from the spec and record their arguments.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").approachActor));

pub const classes = [_][:0]const u8{ "uncached", "model-tile-grouped", "model-tile-plain", "model-group", "model-tile-fallback" };

var waypoint: [*]u32 = undefined;
var plan: c_int = 0;
var new_tile: u32 = 0;

pub const stubs = struct {
    pub fn func_8008E2D4(ctx: *zd.Ctx, group: anytype) [*c]c_uint {
        _ = ctx;
        _ = group;
        return @ptrCast(waypoint);
    }
    pub fn func_800DFA54(ctx: *zd.Ctx, group: anytype, a: anytype, b: anytype, out: anytype) void {
        _ = ctx;
        _ = group;
        _ = a;
        _ = b;
        const bytes: [*]u8 = @ptrCast(out);
        std.mem.writeInt(u32, bytes[0..4], new_tile, .little);
    }
    pub fn func_800E24EC(ctx: *zd.Ctx, state: anytype, option: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = option;
        return plan;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x200, 16);
    for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
    const id = ctx.range(c_int, 0, 16);
    const table = v.g.D_800F4538();
    for (0..17) |i| {
        const model = ctx.allocBytes(0x80, 16);
        for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
        if (ctx.chance(2)) model[0xa] &= 0xE0;
        if (ctx.chance(2)) model[0xc] &= 0xF0;
        std.mem.writeInt(u32, model[0x60..][0..4], ctx.pick(u32, &.{ 0, 0xFF, 0x100, 0x200, 0xFF00, 0xFFFFFFFF, 0x00FF0000 }), .little);
        table[i] = @ptrCast(model);
    }
    const scratch: [*]u8 = @ptrFromInt(zd.scratchpad);
    for (0..17) |i| std.mem.writeInt(u32, scratch[0x37c + 4 * i ..][0..4], if (ctx.chance(5)) 0 else ctx.int(u32) | 1, .little);
    std.mem.writeInt(i16, state[0x15e..][0..2], @bitCast(ctx.pick(u16, &.{ 0, 0x38, 0x8, 0x30, 0xFFFF, 0x7 })), .little);
    state[0x29] = ctx.edge(u8, &.{ 0, 1, 2, 0x80 });
    if (ctx.chance(2)) state[0xe5] = @intCast(id);
    waypoint = @ptrCast(@alignCast(ctx.allocBytes(0x20, 16)));
    for (0..8) |i| waypoint[i] = ctx.int(u32);
    plan = ctx.edge(c_int, &.{ 0, 0, 1, -1, 5 });
    new_tile = ctx.pick(u32, &.{ 0, 0xFF, 0x00FF00FF, 0xFFFFFFFF, 0x00010002 });
    return .{ state, id, ctx.pick(c_int, &.{ 0, 0x100, -1, 0x7FFFFFFF }), ctx.pick(c_int, &.{ 0, 1, 2, -1 }) };
}

pub fn classify(args: Args) usize {
    const id: usize = @intCast(args[1]);
    const scratch: [*]const u8 = @ptrFromInt(zd.scratchpad);
    if (std.mem.readInt(u32, scratch[0x37c + 4 * id ..][0..4], .little) == 0) return 0;
    const state = args[0];
    const model: [*]const u8 = @ptrCast(v.g.D_800F4538()[id].?);
    const busy: u16 = @bitCast(std.mem.readInt(i16, state[0x15e..][0..2], .little) & 0x38);
    if (busy == 0 and model[0xa] & 0x1f != 0) return if (std.mem.readInt(u32, model[0x60..][0..4], .little) & 0xff00 != 0) 1 else 2;
    if ((state[0x29] | @as(u8, @truncate(busy))) != 0 and model[0xc] & 0xf != 0) return 3;
    return 4;
}
