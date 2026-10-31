//! Copies a block into a destination (allocating one if none is given) and
//! relocates four of its words by the block's address (src/BATTLE/BATTLE.PRG/30DB0.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009A028 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies `size` bytes from `src` into `dst` (allocated when null) and adds the block's guest address to words 12..15.
pub fn func_8009A028(dst: [*c]c_int, src: ?*anyopaque, size: c_int) callconv(.c) ?*anyopaque {
    var out = dst;
    if (out == null) {
        out = @ptrCast(@alignCast(c.vs_main_allocHeap(psx.conv(c_uint, size))));
    }

    _ = c.memcpy(out, src, psx.conv(usize, size));

    out[12] = psx.conv(c_int, psx.conv(c_uint, out[12]) +% psx.addr(out));
    out[13] = psx.conv(c_int, psx.conv(c_uint, out[13]) +% psx.addr(out));
    out[14] = psx.conv(c_int, psx.conv(c_uint, out[14]) +% psx.addr(out));
    out[15] = psx.conv(c_int, psx.conv(c_uint, out[15]) +% psx.addr(out));

    return @ptrCast(out);
}
