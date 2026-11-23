//! The port arena (memory lane S6): where the decoded objects of the game's files live. Never the game heap, so decoding
//! does not change the game's allocation pattern (the raw file buffers stay in the heap exactly as the original allocated
//! and freed them). A fixed window range ROOD_PORT_BASE .. + ROOD_PORT_SIZE (psx_types.h), mapped with the rest of the
//! arenas; the snapshot, the Oracle's digest and the in-process rewind save all of it as the "port" range, so a decoded
//! object survives them like any game state, and every pointer in it (into the heap, into other port objects) stays
//! valid because both arenas are at fixed addresses.
//!
//! Layout: a header (the bump offset, the high-water mark, one root slot per decoded format, the free lists by size
//! class), then 16-byte units. Everything in the header is an offset from the arena start, so the range is
//! position-independent data.
//!
//! Policy: the capacity is the range's size, saved whole; a larger decoded set is a deliberate change of
//! ROOD_PORT_SIZE (a snapshot only loads into a build with the same range), never a side effect. Objects are at most
//! `max_object` bytes (a decoder of a larger table splits it); freed blocks go to a free list of their size class,
//! so a format that decodes again (a new file) reuses its space and the arena does not grow with play time. Each
//! decoded format owns a root slot: `put` frees what the root held and allocates the new record, `clear` frees it,
//! so an aborted load that never reaches its release cannot leak. Allocation failure (arena full) returns null and the
//! decoder fails the way the game's own allocation failures do. `high` is the most the arena has held, for tuning.
const std = @import("std");
const psx = @import("psx.zig");

/// One root slot per decoded format.
pub const Root = enum(u8) { wep, zud, _ };
const roots = 8;
const unit = 16;
const classes = 64;
/// The largest object: the biggest size class.
pub const max_object = classes * unit;

const Header = extern struct {
    /// Offset from the arena start of the next never-used byte.
    top: u32,
    /// The highest `top` has been.
    high: u32,
    roots: [roots]u32,
    /// Offset of the first free block of each size class (units - 1); a free block's first word is the next one.
    free: [classes]u32,
};
const header_size = (@sizeOf(Header) + unit - 1) & ~@as(u32, unit - 1);

/// Where the arena is and how big (psx_types.h ROOD_PORT_BASE and ROOD_PORT_SIZE, defined once, in boot_runtime.c).
inline fn base() usize {
    return psx.Global(usize, "rood_port_base").ptr().*;
}
inline fn capacity() u32 {
    return psx.Global(c_uint, "rood_port_size").ptr().*;
}

inline fn header() *Header {
    return @ptrFromInt(base());
}

inline fn at(offset: u32) [*]u8 {
    return @ptrFromInt(base() + offset);
}

fn init() *Header {
    const h = header();
    if (h.top < header_size) h.top = header_size; // the arena starts zeroed
    return h;
}

fn classOf(size: usize) ?usize {
    const units = (size + unit - 1) / unit;
    if (units == 0 or units > classes) return null;
    return units - 1;
}

/// A zeroed block of `size` bytes, 16-byte aligned; null when the arena is full or `size` exceeds `max_object`.
pub fn allocBytes(size: usize) ?[*]u8 {
    const class = classOf(size) orelse return null;
    const h = init();
    const bytes = (class + 1) * unit;
    var offset = h.free[class];
    if (offset != 0) {
        h.free[class] = std.mem.readInt(u32, at(offset)[0..4], .little);
    } else {
        if (h.top + bytes > capacity()) return null;
        offset = h.top;
        h.top += @intCast(bytes);
        if (h.top > h.high) h.high = h.top;
    }
    const p = at(offset);
    @memset(p[0..bytes], 0);
    return p;
}

fn freeBytes(p: [*]u8, size: usize) void {
    const class = classOf(size).?;
    const h = header();
    const offset: u32 = @intCast(@intFromPtr(p) - base());
    std.mem.writeInt(u32, p[0..4], h.free[class], .little);
    h.free[class] = offset;
}

/// The record `root` holds, or null.
pub fn get(comptime T: type, root: Root) ?*T {
    const offset = header().roots[@intFromEnum(root)];
    if (offset == 0) return null;
    return @ptrCast(@alignCast(at(offset)));
}

/// Frees what `root` held, then allocates a zeroed T as its record. Null when the arena is full (the root is then empty).
pub fn put(comptime T: type, root: Root) ?*T {
    comptime if (@sizeOf(T) > max_object) @compileError("port arena: object larger than max_object");
    clear(T, root);
    const p = allocBytes(@sizeOf(T)) orelse return null;
    header().roots[@intFromEnum(root)] = @intCast(@intFromPtr(p) - base());
    return @ptrCast(@alignCast(p));
}

/// Frees `root`'s record (of type T).
pub fn clear(comptime T: type, root: Root) void {
    const h = init();
    const offset = h.roots[@intFromEnum(root)];
    if (offset == 0) return;
    freeBytes(at(offset), @sizeOf(T));
    h.roots[@intFromEnum(root)] = 0;
}
