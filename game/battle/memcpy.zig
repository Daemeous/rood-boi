//! Byte copy of whole 8-byte blocks for battle (src/BATTLE/BATTLE.PRG/573B8.c).
const c = @import("c_573B8");

/// orig: vs_battle_memcpy (src/BATTLE/BATTLE.PRG/573B8.c)
/// Copies numBytes (a positive multiple of 8) from src to dst, four shorts at a time.
pub fn memcpy(dst: ?*anyopaque, src: ?*const anyopaque, numBytes: c_int) callconv(.c) void {
    // Addresses are kept as integers: the C steps `to` and `from` by 8 bytes and
    // compares `from` with `src + numBytes` (the int sign-extends to pointer size).
    var to: usize = @intFromPtr(dst);
    var from: usize = @intFromPtr(src);
    const end: usize = from +% @as(usize, @bitCast(@as(isize, numBytes)));
    if (numBytes <= 0 or (numBytes & 7) != 0) {
        c.pc_unimplemented("vs_battle_memcpy size not a positive multiple of 8");
    }
    // do { ... } while (from != end): the first block is always copied.
    while (true) {
        const t: [*]align(1) i16 = @ptrFromInt(to);
        const f: [*]align(1) const i16 = @ptrFromInt(from);
        t[0] = f[0];
        t[1] = f[1];
        t[2] = f[2];
        t[3] = f[3];
        from +%= 8;
        to +%= 8;
        if (from == end) break;
    }
}
