//! Offsets the colour bytes of packed primitives in a buffer, by (dr, dg) per
//! primitive, for the two primitive layouts (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;

/// Byte at a guest address, as the C reads it through `u_char*`.
inline fn byteAt(addr: usize) *align(1) u8 {
    return @ptrFromInt(addr);
}

/// `*p += delta` on a `u_char`: promoted to int, then truncated back to 8 bits.
fn bumpByte(addr: usize, delta: c_int) void {
    const b = byteAt(addr);
    b.* = psx.conv(u8, @as(c_int, b.*) +% delta);
}

/// orig: func_80099E7C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Adds (dr, dg) to the first two colour bytes of each primitive in `buf`, `entries` of them, for the layout `mode` selects.
pub fn func_80099E7C(buf: ?*anyopaque, entries: c_int, mode: c_int, dr: c_int, dg: c_int) callconv(.c) void {
    var p: usize = @intFromPtr(buf);
    var i: c_int = 0;

    if (mode == 0) {
        while (i < entries) : (i +%= 1) {
            var count: c_int = 0;
            if (byteAt(p).* == 0x24) {
                p +%= 0xA;
                count = 3;
            } else {
                p +%= 0xC;
                count = 4;
            }

            var j: c_int = 0;
            while (j < count) : (j +%= 1) {
                bumpByte(p, dr);
                bumpByte(p +% 1, dg);
                p +%= 2;
            }
        }
    } else {
        while (i < entries) : (i +%= 1) {
            var count: c_int = 0;
            if (byteAt(p +% 11).* == 0x34) {
                bumpByte(p +% 6, dr);
                bumpByte(p +% 7, dg);
                p +%= 0x14;
                count = 2;
            } else {
                p +%= 0x18;
                count = 4;
            }

            var j: c_int = 0;
            while (j < count) : (j +%= 1) {
                bumpByte(p, dr);
                bumpByte(p +% 1, dg);
                p +%= 2;
            }
        }
    }
}
