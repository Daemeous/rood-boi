//! Zeroes memory from the end (src/BATTLE/BATTLE.PRG/5BF94.c).

/// orig: vs_battle_rMemzero (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Zeroes the words below base + size, from the top down, until base (size a positive multiple of 4: the C loops past base otherwise).
pub fn rMemzero(base: ?*anyopaque, size: c_int) callconv(.c) void {
    const start = @intFromPtr(base);
    var p: usize = start +% @as(usize, @bitCast(@as(isize, size)));
    while (true) {
        p -%= 4;
        @as(*align(1) c_int, @ptrFromInt(p)).* = 0;
        if (start == p) break;
    }
}
