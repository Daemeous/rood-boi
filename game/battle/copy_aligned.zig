//! Aligned-prefix copy: copies 2-byte units until the remaining count is a multiple of 8, then hands the rest to vs_battle_memcpy (src/BATTLE/BATTLE.PRG/573B8.c).
const c = @import("c_573B8");

/// orig: vs_battle_copyAligned (src/BATTLE/BATTLE.PRG/573B8.c)
/// Copies `numBytes` bytes, 2 at a time while the count has any of its low 3 bits set, then the rest with vs_battle_memcpy.
pub fn copyAligned(dst: ?*anyopaque, src: ?*const anyopaque, numBytes: c_int) callconv(.c) void {
    var to: [*c]align(1) c_short = @ptrCast(dst);
    var from: [*c]align(1) const c_short = @ptrCast(src);
    var remaining: c_int = numBytes;
    while ((remaining & 7) != 0) {
        to[0] = from[0];
        to += 1;
        from += 1;
        remaining -%= 2;
    }
    _ = c.vs_battle_memcpy(to, from, remaining);
}
