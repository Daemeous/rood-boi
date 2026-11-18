//! The map scene file (ARM: SMALL/SCENnnn.ARM, and the MPD's section F), read in place from its heap buffer. The layout, all little-endian:
//!   +0       i32 room count N
//!   +4       N room records of 12 bytes: i32 visited, u32 size of the room's blob (after initNames: its offset from the buffer's start),
//!            i16 zone id, i16 map id + 1
//!   then     the N room blobs, one after another (the map screen's vertices and lines)
//!   then     N name records of 36 bytes: 32 bytes of name, i16 prev, i16 next (the neighbouring room indices the map walks)
//! Every word is a size or an offset from the buffer: none holds an address.
pub const room_size = 12;
pub const name_size = 36;

/// Turns each room record's size into the offset of its blob from the buffer's start, in the same word, and returns the first byte after
/// the blobs: the name records. `count` is not checked against the file (a table with no rooms is not a scene the game has).
pub fn initNames(table: [*]u8, count: u32) [*]u8 {
    var blob: [*]u8 = table + 4 + @as(usize, count) * room_size;
    var i: u32 = 0;
    while (i < count) : (i += 1) {
        const size: *align(1) u32 = @ptrCast(table + 4 + @as(usize, i) * room_size + 4);
        const length = size.*;
        size.* = @truncate(@intFromPtr(blob) -% @intFromPtr(table));
        blob += length;
    }
    return blob;
}
