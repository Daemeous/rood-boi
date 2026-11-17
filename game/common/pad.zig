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

/// The same as a 32-bit guest address, for the words the game keeps its own addresses in (the image is linked
/// below 4 GB, as every global is).
pub inline fn guest(offset: usize) u32 {
    return @intCast(addr(offset));
}

/// A typed view of the bytes at `offset`, for a file-scope view of a scratch area.
pub inline fn ptr(comptime T: type, comptime offset: usize) *T {
    return @ptrCast(@alignCast(&vs_pad[offset]));
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
