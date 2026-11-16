//! Copies a model block into a destination (allocating one if none is given) (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_content_copyModelGeometry (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies `size` bytes from `src` into `dst` (allocated when null) (the header's four offsets stay file offsets: add them to the block's address).
pub fn vs_content_copyModelGeometry(dst: [*c]c_int, src: ?*anyopaque, size: c_int) callconv(.c) ?*anyopaque {
    var out = dst;
    if (out == null) {
        out = @ptrCast(@alignCast(c.vs_main_allocHeap(psx.conv(c_uint, size))));
    }

    _ = c.memcpy(out, src, psx.conv(usize, size));

    return @ptrCast(out);
}
