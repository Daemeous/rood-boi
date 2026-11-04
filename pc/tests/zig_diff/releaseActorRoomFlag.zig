//! zig_diff spec: func_800E6EB0 -> release_room_flag.releaseActorRoomFlag (D2: pc/src/battle_movement.c).
//! Inputs: an actor id 0..15; its slot of vs_battle_actors and of D_800F5878
//! each empty a quarter of the time (both empty returns; a missing AI entry
//! reads PS1 low RAM, which the spec fills: the flag word at 0x170, the model
//! pointer at 0x58 and the type byte at 9). The actor's type byte is a
//! member of the set 0x4800 (11, 14, 43, 46: the `& 31`), a near miss (27, 30,
//! 12, 13, 15) or any byte; the flag word's bits 8-15 are zero a third of the
//! time; the model's byte 0xC has a zero low nibble half the time (the high
//! nibble random). func_800E4BD8 is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").releaseActorRoomFlag));

pub const classes = [_][:0]const u8{ "both-empty", "state-from-low-ram", "wrong-type", "no-flags", "model-busy", "released" };

fn word(p: [*]u8, off: usize) *align(1) u32 {
    return @ptrCast(p + off);
}

fn lowRam() [*]u8 {
    return @ptrFromInt(zd.low_ram);
}

fn member(kind: u8) bool {
    return (@as(u32, 1) << @intCast(kind & 31)) & 0x4800 != 0;
}

fn record(ctx: *zd.Ctx, state: [*]u8) void {
    var flags = ctx.int(u32);
    if (ctx.chance(3)) flags &= ~@as(u32, 0xff00);
    word(state, 0x170).* = flags;
    const model = ctx.allocBytes(0x20, 16);
    for (model[0..0x20]) |*byte| byte.* = ctx.int(u8);
    if (ctx.chance(2)) model[0xc] &= 0xf0;
    word(state, 0x58).* = @intCast(@intFromPtr(model));
    // A second, independent model at the neighbouring word (0x5C): a port
    // that read the wrong word still gets a valid pointer, and a different byte 0xC.
    const other = ctx.allocBytes(0x20, 16);
    for (other[0..0x20]) |*byte| byte.* = ctx.int(u8);
    if (ctx.chance(2)) other[0xc] &= 0xf0 else other[0xc] |= 1;
    word(state, 0x5c).* = @intCast(@intFromPtr(other));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 15);
    const index: usize = @intCast(id);
    if (ctx.chance(4)) {
        v.g.vs_battle_actors()[index] = null;
    } else {
        const actor = ctx.allocBytes(0x20, 16);
        for (actor[0..0x20]) |*byte| byte.* = ctx.int(u8);
        actor[9] = ctx.pick(u8, &.{ 11, 14, 43, 46, 27, 30, 12, 13, 15, 0 });
        v.g.vs_battle_actors()[index] = @ptrCast(actor);
    }
    if (ctx.chance(4)) {
        v.g.D_800F5878()[index] = null;
        const low = lowRam();
        low[9] = ctx.pick(u8, &.{ 11, 14, 43, 46, 27, 30, 12, 0 });
        record(ctx, low);
    } else {
        const state = ctx.allocBytes(0x200, 16);
        for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
        record(ctx, state);
        v.g.D_800F5878()[index] = @ptrCast(state);
    }
    return .{id};
}

pub fn classify(args: Args) usize {
    const index: usize = @intCast(args[0]);
    const actor = v.g.vs_battle_actors()[index];
    const entry = v.g.D_800F5878()[index];
    if (actor == null and entry == null) return 0;
    const state: [*]u8 = if (entry) |e| @ptrCast(e) else lowRam();
    const kind: u8 = if (actor) |a| @as([*]u8, @ptrCast(a))[9] else lowRam()[9];
    if (!member(kind)) return 2;
    if (word(state, 0x170).* & 0xff00 == 0) return 3;
    const model: [*]u8 = @ptrFromInt(word(state, 0x58).*);
    if (model[0xc] & 0xf != 0) return 4;
    return if (entry == null) 1 else 5;
}
