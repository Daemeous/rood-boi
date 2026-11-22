//! The scratchpad's remaining bytes (vs_pad, pc/src/game/include/scratch.h): the PS1 kept the per-file scratch
//! views, the actor transform and AI slots, and the collision and GTE work areas in its 1 KB scratchpad at
//! 0x1F800000. They overlay one another (a view of one file is another's at a different time), so they stay one
//! block, indexed by their PS1 offset from 0x1F800000. The bytes below 0x68 are the packet cursor and the view
//! (vs_scratch, vs_view): named globals, not here.
const psx = @import("psx.zig");

/// Hidden, so Windows reaches it directly rather than through a .refptr stub (the build rejects those); not
/// psx.Global: file-scope views (`pad.ptr` in a `const`) need its address at compile time.
const vs_pad: *align(16) [0x408]u8 = if (psx.library or psx.host)
    @extern(*align(16) [0x408]u8, .{ .name = "vs_pad" }) // (through the GOT, as psx.Global does there)
else
    @extern(*align(16) [0x408]u8, .{ .name = "vs_pad", .visibility = .hidden });

/// The address of the byte at `offset` (a PS1 offset from 0x1F800000, 0x68..0x3FF).
pub inline fn addr(offset: usize) usize {
    return @intFromPtr(vs_pad) + offset;
}

/// A typed view of the bytes at `offset`, for a file-scope view of a scratch area.
pub inline fn ptr(comptime T: type, comptime offset: usize) *T {
    return @ptrCast(@alignCast(@as([*]u8, @ptrCast(vs_pad)) + offset));
}

/// The scratch bytes at `offset` as a T (a PS1 offset), checked at compile time to lie inside the 0x408 bytes (the 1 KB pad and 8 bytes for the real pointers that follow its last overlays): the same
/// address `@ptrFromInt(pad.addr(offset))` gave, for the typed overlays (their size and offset are in one place).
pub inline fn at(comptime T: type, comptime offset: usize) *T {
    comptime if (offset + @sizeOf(T) > 0x408) @compileError("pad.at: the overlay runs past the scratchpad");
    return @ptrFromInt(addr(offset));
}

/// The scratch bytes at `offset` as a run of T (a table), from the offset to the end of the pad at most.
pub inline fn many(comptime T: type, comptime offset: usize) [*]T {
    comptime if (offset + @sizeOf(T) > 0x408) @compileError("pad.many: the table starts past the scratchpad");
    return @ptrFromInt(addr(offset));
}

/// The navigation pointers the battle AI reads (pc/src/game/include/scratch.h vs_nav_t): real pointers, filled each actor
/// update by pc_actor_scratch; they were 4-byte guest words in the pad (0x37C, 0x3BC, 0x3C0, 0x3C4, 0x3F4).
pub const Nav = extern struct {
    aiStates: [17]?*anyopaque, // vs_battle_aiState*, by actor id
    floors: ?[*][32]u16, // the floor grid (D_800F58D0)
    cells: ?[*][32]u16, // the navigation cells (D_800F58B8)
    bitmap: ?[*]u8, // the occupancy bitmap (D_800F58CC)
    exits: ?[*]u8, // the room's exit table (D_800F58C0); non-null when the bitmap is active
};
/// The AI state of actor `id`, unchecked as the pad's table was (a slot past the 17 reads on).
pub inline fn aiState(id: anytype) ?*anyopaque {
    const index: isize = @intCast(id);
    return @as(*const ?*anyopaque, @ptrFromInt(@intFromPtr(&nav.aiStates) +% @as(usize, @bitCast(index *% 8)))).*;
}
pub const nav: *Nav = if (psx.library or psx.host)
    @extern(*Nav, .{ .name = "vs_nav" })
else
    @extern(*Nav, .{ .name = "vs_nav", .visibility = .hidden });

/// The actor transform block (scratch.h vs_xform): the registers the actor transform functions hand one another, a T over its
/// start (the views are in the lifetime table, pc/docs/memory_layouts/s3b-pad-overlays.tsv).
pub inline fn xform(comptime T: type) *T {
    comptime if (@sizeOf(T) > 0xA4) @compileError("pad.xform: the view is larger than the block");
    return @ptrCast(@alignCast(xform_block));
}
const xform_block: *align(16) [0xA4]u8 = if (psx.library or psx.host)
    @extern(*align(16) [0xA4]u8, .{ .name = "vs_xform" })
else
    @extern(*align(16) [0xA4]u8, .{ .name = "vs_xform", .visibility = .hidden });
/// The actor collision workspace (scratch.h vs_collision), a T over its start.
pub inline fn collision(comptime T: type) *T {
    comptime if (@sizeOf(T) > 0x4C) @compileError("pad.collision: the view is larger than the block");
    return @ptrCast(@alignCast(collision_block));
}
const collision_block: *align(16) [0x4C]u8 = if (psx.library or psx.host)
    @extern(*align(16) [0x4C]u8, .{ .name = "vs_collision" })
else
    @extern(*align(16) [0x4C]u8, .{ .name = "vs_collision", .visibility = .hidden });

/// The AI movement frame (pc/src/game/include/scratch.h vs_move_t): the movement code's own struct, filled each actor update by
/// pc_actor_scratch (the pad's bytes 0x3C8..0x3FF held it, shared with the collision and animation overlays).
pub const Move = extern struct {
    command: c_int, // the action vs_battle_aiIssueActorCommand is carrying out
    gap3CC: [4]u8,
    cache: extern union {
        word: u32, // the three caches below, cleared together
        bits: extern struct {
            flyTested: u8, // per direction: whether it is known to be passable (vs_battle_aiCanFlyInDirection)
            flyPassable: u8, // and what it is
            blockTested: u8, // per direction: its blocking actor is known
            gap3D3: u8,
        },
    },
    blocker: [8]u8, // that actor's id + 1, by direction
    width: u8, // the room's navigation grid size
    depth: u8,
    gap3DE: [2]u8,
    lookup: [12]i8, // the 3x3 neighbourhood's step directions
    directions: [8]u8, // a direction's packed step (D_800F16EC_t)
    gap3F4: [4]u8,
    randomState: u32, // the movement random step's seed (its low half) ...
    randomIncrement: u32, // ... and increment
    comptime {
        if (@sizeOf(Move) != 0x38 or @offsetOf(Move, "blocker") != 0xC or @offsetOf(Move, "width") != 0x14 or @offsetOf(Move, "directions") != 0x24 or @offsetOf(Move, "randomState") != 0x30)
            @compileError("Move does not match vs_move_t");
    }
};
pub const move: *Move = if (psx.library or psx.host)
    @extern(*Move, .{ .name = "vs_move" })
else
    @extern(*Move, .{ .name = "vs_move", .visibility = .hidden });
/// The packed direction steps as the caller's view of them (D_800F16EC_t, one byte each).
pub inline fn directions(comptime T: type) [*]align(1) T {
    comptime if (@sizeOf(T) != 1) @compileError("pad.directions: a direction is one byte");
    return @ptrCast(&move.directions);
}
/// A byte of the movement frame's tables by index, unchecked as the pad's bytes were (the index is a direction, 0..7, or a
/// neighbour cell, 0..11): the actor's blocking-actor cache, packed direction steps and neighbourhood lookup.
pub inline fn blockerAt(index: anytype) *u8 {
    return @ptrFromInt(@intFromPtr(&move.blocker) + @as(usize, @intCast(index)));
}
pub inline fn directionAt(index: anytype) *u8 {
    return @ptrFromInt(@intFromPtr(&move.directions) + @as(usize, @intCast(index)));
}
pub inline fn lookupAt(index: anytype) *u8 {
    return @ptrFromInt(@intFromPtr(&move.lookup) + @as(usize, @intCast(index)));
}
