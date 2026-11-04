//! zig_diff spec: func_800E5308 -> pc.update_actor_navigation.updateActorNavigation (D2: pc/src/actor_update.c).
//! Inputs: a list (vs_battle_actors[0]) of up to three nodes (flags byte 8
//! with the 0xC0 skip bits set or clear, id at +4 of 0 to 16, the next link
//! at +0); the scratchpad's state table (word 0x37C + 4 id) holding a base
//! block for most ids, or zero (the C then reads word 0x18C of PS1 address 0,
//! which `prepare` sets to 0, so the actor is skipped); each base's word at
//! +0x18C is the AI state's address or zero. The state has byte 0x125 (moving
//! flag), the target word at 0x160, the 0x168 word (equal to the tile in the
//! masked bits or not), byte 0x19B (the continuation flag), a countdown
//! halfword at 0x156 and the packed word at 0x190, all edge or random. The
//! actor model of each id (D_800F4538) has its tile word (+0x5C, the grid
//! cell inside 32 x 32) and position words. The stub of func_800E4660 returns
//! one planned distance (0, 1023, 1024, 5000 or negative) for the whole run;
//! the other callees' stubs record their arguments. Beyond that: a list of 18
//! or 19 nodes (the cycle guard's pc_unimplemented, whose stub returns), a node
//! with an id of 19 (the table guard; the scratchpad's state words, the
//! model table (room in specs.toml) and the models are filled for 20 ids), a
//! tile outside the 32 x 32 grid (the grid block is then 8416 halfwords), and,
//! on the last node when it reaches its clear, link bits 0 and 1 of byte 3 set
//! (the `actor[3] &= 0xfc` strips them; kept, they point outside the host's
//! memory: a crash, which the mask mutations expect).
//!
//! The scratchpad refresh: the harness stubs `func_800E65DC` (a recording
//! no-op), but if the C reference ever runs the real `pc_actor_scratch` the
//! scratchpad's state table (words 0x37C..0x3BC) is rebuilt from
//! D_800F5878[0..16], words 0x3BC (id 16), 0x3C0 (id 17) and 0x3C4 (id 18)
//! are overwritten from D_800F58D0, D_800F58B8 and D_800F58CC, and word 0x3DC
//! is read through D_800F58BC + 4 (a random D_800F58BC: a wild read). So this
//! spec makes both worlds agree: D_800F5878 holds the same entries as the
//! scratchpad table, D_800F58BC is 0 (the C then reads PS1 address 4), and the
//! nodes only use ids whose scratchpad word `pc_actor_scratch` leaves alone or
//! rebuilds from the table: 0 to 15 and, for the table guard, 19 (word 0x3C8).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_update");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").updateActorNavigation));

pub const classes = [_][:0]const u8{ "no-actors", "skipped-actor", "no-state", "idle-near", "idle-far-route", "idle-far-continuation", "moving-retarget", "moving-same", "bad-id", "long-list", "wide-tile" };

/// The C and the port report the unimplemented paths with their own literal.
pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

var distance: c_int = 0;

pub const stubs = struct {
    pub fn func_800E4660(ctx: *zd.Ctx, a: c_int, b: c_int, c: c_int) c_int {
        _ = .{ ctx, a, b, c };
        return distance;
    }
};

fn guest(p: anytype) u32 {
    return @intCast(@intFromPtr(p));
}

fn put(block: [*]u8, offset: usize, value: u32) void {
    std.mem.writeInt(u32, block[offset..][0..4], value, .little);
}

/// Whether a node with this id gets past the state lookup (the scratchpad
/// entry's base block holds a state address, or the zero page's word is 0).
fn reachesClear(id: u32) bool {
    const scratch: [*]volatile u32 = @ptrFromInt(0x1f800000);
    const entry = scratch[0x37c / 4 + id];
    if (entry == 0) return false;
    const base: [*]const u8 = @ptrFromInt(entry);
    return std.mem.readInt(u32, base[0x18c..][0..4], .little) != 0;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    distance = ctx.edge(c_int, &.{ 0, 1023, 1024, 5000, -1, 100 });
    const scratch: [*]volatile u32 = @ptrFromInt(0x1f800000);
    const low: *volatile u32 = @ptrFromInt(0x8000018c);
    low.* = 0;

    // Grid cells (32 x 32 halfwords; a tile outside it reads on, up to row and column 255), random.
    const grid = ctx.allocBytes(8416 * 2, 16);
    for (grid[0 .. 8416 * 2]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F58B8().* = guest(grid);

    // Per id: the model, and (mostly) a base block holding the state pointer.
    const models = v.g.D_800F4538();
    const table = v.g.D_800F5878();
    v.g.D_800F58BC().* = 0;
    const wide = ctx.chance(8);
    const limit: u32 = if (wide) 255 else 31;
    for (0..20) |id| {
        const model = ctx.allocBytes(0x80, 16);
        for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
        // The tile: row and column random, or the grid's last cell (1023) and the first past it (1024).
        const cell_bits = if (ctx.chance(6)) ctx.edge(u32, &.{ (31 << 16) | 31, 32 << 16 }) else (ctx.range(u32, 0, limit) << 16) | ctx.range(u32, 0, limit);
        put(model, 0x5c, cell_bits | (ctx.int(u32) & 0xFF00FF00));
        models[id] = @ptrCast(model);
        const roll = ctx.range(u8, 0, 5);
        if (roll == 0) {
            scratch[0x37c / 4 + id] = 0;
        } else {
            const base = ctx.allocBytes(0x200, 16);
            for (base[0..0x200]) |*byte| byte.* = ctx.int(u8);
            if (roll == 1) {
                put(base, 0x18c, 0);
            } else {
                const state = ctx.allocBytes(0x300, 16);
                for (state[0..0x300]) |*byte| byte.* = ctx.int(u8);
                // The AI-state id: the C hands it to func_800E5240, whose table (D_800F5878,
                // 17 slots, set above) it indexes; an id of 17 or more reads past the table.
                state[0x88] = ctx.range(u8, 0, 16);
                state[0x125] = ctx.edge(u8, &.{ 0, 0, 1, 0xFF });
                state[0x19b] = ctx.edge(u8, &.{ 0, 0, 1, 0xFF });
                std.mem.writeInt(u16, state[0x156..][0..2], ctx.edge(u16, &.{ 0, 1, 2, 0xFFFF }), .little);
                put(state, 0x190, ctx.pick(u32, &.{ 0, 0x20, 0xFE0, 0xFFFFFFFF, 0x3E0 }));
                put(state, 0x160, ctx.int(u32));
                const tile = std.mem.readInt(u32, @as([*]u8, @ptrCast(models[id].?))[0x5c..][0..4], .little);
                put(state, 0x168, if (ctx.chance(2)) (ctx.int(u32) & 0xFF00FF00) | (tile & 0xFF00FF) else ctx.int(u32));
                put(base, 0x18c, guest(state));
            }
            scratch[0x37c / 4 + id] = guest(base);
        }
        if (id < 17) table[id] = if (scratch[0x37c / 4 + id] == 0) null else @ptrFromInt(scratch[0x37c / 4 + id]);
    }

    // The actor list.
    var next: u32 = 0;
    var last: [*]u8 = undefined;
    const count = if (ctx.chance(25)) ctx.range(usize, 18, 19) else ctx.range(usize, 0, 3);
    for (0..count) |index| {
        const node = ctx.allocBytes(0x40, 16);
        for (node[0..0x40]) |*byte| byte.* = ctx.int(u8);
        const id: u32 = if (ctx.chance(12)) ctx.range(u32, 17, 19) else ctx.range(u32, 0, 16);
        put(node, 4, id);
        node[8] = if (ctx.chance(4)) ctx.edge(u8, &.{ 0x40, 0x80, 0xC0 }) else ctx.edge(u8, &.{ 0, 0x3F, 0x01 });
        put(node, 0, next);
        if (index == 0) last = node;
        next = guest(node);
    }
    // The last node's link: zero, or (when the node reaches `actor[3] &= 0xfc`)
    // the bits that statement clears. Only when no earlier node can have
    // taken its state away first: a node of the same id shares the state
    // block, and should func_800E5240 run for real (it is a stub in the
    // harness) a node whose AI-state id (byte 0x88) is the last node's id
    // would zero the base block's state word.
    if (count != 0 and last[8] & 0xC0 == 0) {
        const id = std.mem.readInt(u32, last[4..8], .little);
        var clear = reachesClear(id);
        var link = next;
        while (clear and link != guest(last)) {
            const node: [*]const u8 = @ptrFromInt(link);
            if (node[8] & 0xC0 == 0) {
                const other = std.mem.readInt(u32, node[4..8], .little);
                if (other == id) clear = false;
                if (reachesClear(other)) {
                    const base: [*]const u8 = @ptrFromInt(scratch[0x37c / 4 + other]);
                    const state: [*]const u8 = @ptrFromInt(std.mem.readInt(u32, base[0x18c..][0..4], .little));
                    if (state[0x88] == id) clear = false;
                }
            }
            link = std.mem.readInt(u32, node[0..4], .little);
        }
        if (clear) put(last, 0, ctx.edge(u32, &.{ 0, 0x01000000, 0x02000000, 0x03000000 }));
    }
    v.g.vs_battle_actors()[0] = if (next == 0) null else @ptrFromInt(next);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const scratch: [*]volatile u32 = @ptrFromInt(0x1f800000);
    // The list (the last node's link may have byte-3 bits set, outside the arena: see prepare).
    var length: usize = 0;
    var bad_id = false;
    var wide_tile = false;
    var link: u32 = guest(v.g.vs_battle_actors()[0] orelse return 0);
    const arena = guest(&zd.zdiff_arena);
    while (link >= arena and link < arena + zd.arena_size and length < 40) : (length += 1) {
        const node: [*]const u8 = @ptrFromInt(link);
        if (node[8] & 0xC0 == 0) {
            const id = std.mem.readInt(u32, node[4..8], .little);
            if (id >= 17) bad_id = true;
            if (reachesClear(id)) {
                const tile = std.mem.readInt(u32, @as([*]u8, @ptrCast(v.g.D_800F4538()[id].?))[0x5c..][0..4], .little);
                if (((tile >> 16) & 255) * 32 + (tile & 255) >= 1024) wide_tile = true;
            }
        }
        link = std.mem.readInt(u32, node[0..4], .little);
    }
    if (length > 17) return 9;
    if (bad_id) return 8;
    if (wide_tile) return 10;
    const head: [*]u8 = @ptrCast(v.g.vs_battle_actors()[0] orelse return 0);
    if (head[8] & 0xC0 != 0) return 1;
    const id = std.mem.readInt(u32, head[4..8], .little);
    const entry = scratch[0x37c / 4 + id];
    if (entry == 0) return 2;
    const base: [*]const u8 = @ptrFromInt(entry);
    const state_address = std.mem.readInt(u32, base[0x18c..][0..4], .little);
    if (state_address == 0) return 2;
    const state: [*]const u8 = @ptrFromInt(state_address);
    if (state[0x125] == 0) {
        if (distance < 1024) return 3;
        return if (state[0x19b] != 0) 5 else 4;
    }
    const tile = std.mem.readInt(u32, @as([*]u8, @ptrCast(v.g.D_800F4538()[id].?))[0x5c..][0..4], .little);
    const word = std.mem.readInt(u32, state[0x168..][0..4], .little);
    return if (word & 0xFF00FF != tile & 0xFF00FF) 6 else 7;
}
