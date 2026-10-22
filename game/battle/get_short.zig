//! Reads a little-endian 16-bit value from a byte address (src/BATTLE/BATTLE.PRG/573B8.c).

/// orig: vs_battle_getShort (src/BATTLE/BATTLE.PRG/573B8.c)
/// Reads the halfword at `arg0`: a direct load when the address is even, two byte loads when odd.
pub fn getShort(arg0: [*c]u8) callconv(.c) c_short {
    if ((@intFromPtr(arg0) & 1) == 0) {
        return @as(*align(1) c_short, @ptrCast(arg0)).*;
    }
    const lo: c_int = arg0[0];
    const hi: c_int = arg0[1];
    return @truncate(lo | (hi << 8));
}
