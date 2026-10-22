//! Walks a p_file block chain, dispatching each block by its type tag to its parser (src/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// Block parsers take their block as a pointer; the blocks sit at arbitrary byte offsets, so the data pointer is passed untyped.
const Parse = *const fn (?*anyopaque) callconv(.c) void;

/// The block's data pointer: the bytes after the 4-byte meta word.
inline fn dataOf(cur: *align(1) v.p_file_t) *align(1) anyopaque {
    return @ptrCast(psx.byteAt(u8, cur, 4));
}

/// orig: func_800D8060 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Parses each block of a p_file in order until the end marker (type 0), advancing by each block's size.
pub fn func_800D8060(arg0: ?*v.p_file_t) callconv(.c) void {
    var cur: *align(1) v.p_file_t = arg0.?;
    while (true) {
        const meta: c_int = cur.meta;
        // meta & 0xFFFF0000 is unsigned in C (0xFFFF0000 is an unsigned constant).
        const kind: c_uint = @as(c_uint, @bitCast(meta)) & 0xFFFF_0000;
        switch (kind) {
            0x0 => return,
            0x10000 => @as(Parse, @ptrCast(&c._parsePfileBlock1))(dataOf(cur)),
            0x20000 => @as(Parse, @ptrCast(&c._parsePfileBlock2))(dataOf(cur)),
            0x30000 => @as(Parse, @ptrCast(&c._parsePfileBlock3))(dataOf(cur)),
            0x40000 => @as(Parse, @ptrCast(&c._parsePfileCurveBlock))(dataOf(cur)),
            0x50000 => @as(Parse, @ptrCast(&c._parsePfileBlock5))(dataOf(cur)),
            0x80000 => @as(Parse, @ptrCast(&c._parsePfileBlock8))(dataOf(cur)),
            0x90000 => @as(Parse, @ptrCast(&c._parsePfileBlock9))(dataOf(cur)),
            0xA0000 => @as(Parse, @ptrCast(&c._parsePfileBlock10))(dataOf(cur)),
            0xB0000 => @as(Parse, @ptrCast(&c._parsePfileBlock11))(dataOf(cur)),
            0xC0000 => @as(Parse, @ptrCast(&c._parsePfileBlock12))(dataOf(cur)),
            0xD0000 => @as(Parse, @ptrCast(&c._parsePfileBlock13))(dataOf(cur)),
            else => {},
        }
        // Advance by the block size (meta & 0xFFFF, a byte count), wrapping as C pointer arithmetic does.
        const size: usize = @as(c_uint, @bitCast(meta)) & 0xFFFF;
        cur = @ptrFromInt(@as(usize, @intFromPtr(cur)) +% size);
    }
}
