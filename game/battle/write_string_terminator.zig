//! Writes the string terminator byte (231, as the PC object stores it) at a text buffer's start (src/BATTLE/BATTLE.PRG/5BF94.c).

/// orig: _writeStringTerminator (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores the string terminator byte (231) at `str`; the PC object writes 231, not the source's '\0'.
pub fn writeStringTerminator(str: [*c]u8) callconv(.c) void {
    str[0] = 231;
}
