//! zig_diff spec: func_800DBCEC -> pc.no_hostile_near_target.noHostileNearTarget (D2: pc/src/battle_movement.c).
//! Inputs: the actor list, which the oracle starts at the address of
//! vs_battle_actors itself (its first "node" has the upper half of slot 0 as
//! its id, 0, and the lower half as its link), followed by 0..3 nodes with ids
//! below 16 (relation words at 0x1A4 + id * 4, hostile when bits 6-7 are 3;
//! models D_800F4538[id] with the tile word at 0x5C) or, for the guest-RAM
//! read, an id of 16 or more chosen so that the relation address
//! (0x1A4 + id * 4 from the state) lands in the mapped part of PS1 low RAM
//! (in range, or past the 8 MB mirror limit so pc_unimplemented is called); the
//! relation word there is made non-hostile (the second read, of the model
//! pointer table, cannot be steered into mapped memory too). The
//! engagement table D_800F45E0 holds a block per target; the stub of
//! func_800E45F4 answers the tile distance (below, at and above 3).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").noHostileNearTarget));

pub const classes = [_][:0]const u8{ "no-other-actors", "none-hostile", "hostile-far", "hostile-near", "guest-read", "guest-read-unmapped-range" };

var stage: usize = 0;
var near_value: c_int = 0;

pub const stubs = struct {
    pub fn func_800E45F4(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        return near_value;
    }
};

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

fn put32(p: [*]u8, off: usize, value: u32) void {
    std.mem.writeInt(u32, p[off..][0..4], value, .little);
}

fn randomBlock(ctx: *zd.Ctx, size: usize) [*]u8 {
    const block = ctx.allocBytes(size, 16);
    for (block[0..size]) |*byte| byte.* = ctx.int(u8);
    return block;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const low_ram: [*]u8 = @ptrFromInt(zd.low_ram);
    stage = ctx.range(usize, 0, 5);
    const state = randomBlock(ctx, 0x400);
    const base: u32 = @intCast(@intFromPtr(state));
    const models = v.g.D_800F4538();
    const engaged = v.g.D_800F45E0();
    for (0..16) |i| {
        models[i] = @ptrCast(randomBlock(ctx, 0x80));
        engaged[i] = @ptrCast(randomBlock(ctx, 0x80));
    }
    near_value = switch (stage) {
        3 => ctx.edge(c_int, &.{ 0, 1, 2, -1, -0x80000000 }),
        else => ctx.edge(c_int, &.{ 3, 4, 0x7FFFFFFF, 3 }),
    };
    // The relation words: hostile (bits 6-7 both set) per the stage.
    const hostile_any = stage == 2 or stage == 3;
    for (0..16) |i| {
        var word = ctx.int(u32);
        const hostile = hostile_any and ctx.chance(3);
        word = if (hostile) word | 0xC0 else word & ~@as(u32, if (ctx.chance(2)) 0x80 else 0x40);
        put32(state, 0x1a4 + 4 * i, word);
    }
    if (hostile_any) put32(state, 0x1a4, std.mem.readInt(u32, state[0x1a4..][0..4], .little) | (if (ctx.chance(2)) @as(u32, 0xC0) else 0));
    // The nodes after the first one.
    const count: usize = if (stage == 0) 0 else ctx.range(usize, 1, 3);
    var first: ?[*]u8 = null;
    var previous: ?[*]u8 = null;
    for (0..count) |i| {
        const node = randomBlock(ctx, 0x10);
        put32(node, 0, 0);
        var id: u32 = ctx.range(u32, 0, 15);
        if ((stage == 4 or stage == 5) and i == count - 1) {
            // A relation address inside low RAM (0x80000000 + k) or its mirror
            // past 8 MB (0x00800000 + k, which traps in the oracle).
            const k: u32 = ctx.range(u32, 0, 0x7000) & ~@as(u32, 3);
            const segment = ctx.edge(u32, &.{ 0, 0x80000000, 0xA0000000 });
            const target: u32 = if (stage == 4)
                segment +% ctx.range(u32, 0, 3) * 0x200000 +% k // the 2 MB mirrors below 8 MB
            else
                segment +% (ctx.range(u32, 4, 15) * 0x200000) +% k;
            id = (((target -% base -% 0x1a4) >> 2) | (@as(u32, ctx.range(u32, 0, 3)) << 30));
            // A non-hostile word there.
            const word = ctx.int(u32) & ~@as(u32, 0x40);
            std.mem.writeInt(u32, low_ram[k..][0..4], word, .little);
        }
        put32(node, 4, id);
        if (previous) |p| put32(p, 0, @intCast(@intFromPtr(node))) else first = node;
        previous = node;
    }
    v.g.vs_battle_actors()[0] = if (first) |f| @ptrCast(f) else null;
    return .{ state, ctx.range(c_int, 0, 15) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return stage;
}
