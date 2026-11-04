//! zig_diff spec: func_800E7454 -> pc.unload_actor.unloadActor (D2: pc/src/actor_cleanup.c).
//! Inputs: D_800F58B8 zero a sixth of the time; an actor node (id at +4, 0 to
//! 16, flags byte at 0x1C over 0, 1, 4, 5, 0x10, 0x14 and any value, type
//! halfword at 0x1A over 0xAC, 0xAD, the values around them and any); an
//! actor list of up to three peer nodes whose ids index states in
//! D_800F5878 (all 17 slots filled, state byte 0x16 equal to the node's id
//! or random; the node's own slot is empty a quarter of the time, and then
//! no peer has that id); a grid record whose slots at 0x28..0x34 hold the
//! node, other pointers or zero, with a 16-bit mask at 0x10 (the node's bit
//! set or clear) and a count at 0x12. The stubs record func_800E5240,
//! func_800E678C, func_800D821C, func_800DBF00 and vs_main_freeHeap calls.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_cleanup");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").unloadActor));

pub const classes = [_][:0]const u8{ "no-grid", "flags-clear", "id-zero", "state-missing", "data-block", "bit-set", "bit-clear" };

fn guest(p: anytype) u32 {
    return @intCast(@intFromPtr(p));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const node = ctx.allocBytes(0x40, 16);
    for (node[0..0x40]) |*byte| byte.* = ctx.int(u8);
    const id: u32 = ctx.edge(u32, &.{ 0, 1, 2, 5, 15, 16, 16, 7, 3 });
    std.mem.writeInt(u32, node[4..8], id, .little);
    node[0x1c] = ctx.edge(u8, &.{ 0, 1, 4, 5, 0x10, 0x14, 0x15, 0x08, ctx.int(u8) });
    std.mem.writeInt(u16, node[0x1a..][0..2], ctx.edge(u16, &.{ 0xAC, 0xAD, 0xAB, 0xAE, 0, ctx.int(u16) }), .little);
    v.g.D_800F58B8().* = if (ctx.chance(6)) 0 else guest(ctx.allocBytes(0x40, 16));
    v.g.D_800F590C().* = ctx.int(u32);
    v.g.D_800F5920().* = ctx.pick(u32, &.{ 0, guest(ctx.allocBytes(0x20, 16)) });

    const states = v.g.D_800F5878();
    for (0..17) |slot| {
        const state = ctx.allocBytes(0x200, 16);
        for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
        if (ctx.chance(3)) state[0x16] = @intCast(id);
        states[slot] = @ptrCast(state);
    }
    const own_missing = ctx.chance(4);
    if (own_missing) states[id] = null;

    var next: u32 = 0;
    for (0..ctx.range(usize, 0, 3)) |_| {
        const peer = ctx.allocBytes(0x40, 16);
        for (peer[0..0x40]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(u32, peer[0..4], next, .little);
        var peer_id = ctx.range(u32, 0, 16);
        if (own_missing and peer_id == id) peer_id = (id + 1) % 17;
        std.mem.writeInt(u32, peer[4..8], peer_id, .little);
        next = guest(peer);
    }
    v.g.vs_battle_actors()[0] = if (next == 0) null else @ptrFromInt(next);

    const grid = ctx.allocBytes(0x60, 16);
    for (grid[0..0x60]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F58BC().* = guest(grid);
    var offset: usize = 0x28;
    while (offset <= 0x34) : (offset += 4) {
        std.mem.writeInt(u32, grid[offset..][0..4], ctx.edge(u32, &.{ guest(node), guest(node), 0, 0x80101000 }), .little);
    }
    const bit: u16 = if (id < 16) @as(u16, 1) << @intCast(id) else 0;
    const mask = std.mem.readInt(u16, grid[0x10..][0..2], .little);
    std.mem.writeInt(u16, grid[0x10..][0..2], if (ctx.chance(2)) mask | bit else mask & ~bit, .little);
    return .{node};
}

pub fn classify(args: Args) usize {
    const node = args[0];
    const flags = node[0x1c];
    const id = std.mem.readInt(u32, node[4..8], .little);
    const kind = std.mem.readInt(u16, node[0x1a..][0..2], .little);
    if (v.g.D_800F58B8().* == 0) return 0;
    if (flags & 5 == 0) return 1;
    if (id == 0) return 2;
    if (v.g.D_800F5878()[id] == null) return 3;
    if (kind == 0xAC or kind == 0xAD) return 4;
    const grid: [*]const u8 = @ptrFromInt(v.g.D_800F58BC().*);
    const mask = std.mem.readInt(u16, grid[0x10..][0..2], .little);
    // Word bit 16 does not exist in the halfword: id 16 never finds its bit.
    return if (id < 16 and mask & (@as(u16, 1) << @intCast(id)) != 0) 5 else 6;
}
