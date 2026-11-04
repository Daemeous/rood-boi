//! Zero-filling whole words (D2: 7B5DC.s; oracle pc/src/battle_helpers.c).
const c = @import("c_battle_helpers");

/// orig: func_800E4288 (pc/src/battle_helpers.c)
/// Zeroes `bytes >> 2` words at the destination. A span under 4 bytes or an
/// unaligned destination is reported through pc_unimplemented, which the
/// original (and so this port) does not stop at.
pub fn zeroFill(destination: [*]u8, bytes: c_uint) callconv(.c) void {
    if (bytes < 4 or @intFromPtr(destination) & 3 != 0) c.pc_unimplemented("battle zero-fill invalid span");
    const words: [*]align(1) c_uint = @ptrCast(destination);
    var i: c_uint = 0;
    while (i < bytes >> 2) : (i +%= 1) words[i] = 0;
}
