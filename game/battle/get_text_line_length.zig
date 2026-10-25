//! Measures a text line's pixel width from the character widths, for text layout
//! (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_battle_getTextLineLength (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the width of the line at `str` in characters of six pixels, stopping at its terminator.
pub fn getTextLineLength(str: [*c]u8) callconv(.c) c_int {
    const widths = v.g.vs_battle_characterWidths();
    var s = str;
    var length: c_int = 0;

    while (true) {
        var c: u8 = s[0];
        s += 1;
        if (c < 229) {
            length +%= @as(c_int, widths[c]);
            continue;
        } else if (c == 229 or c == 231 or c == 232) {
            // line terminators
            return @divTrunc(length, 6);
        } else if (c == 250) {
            // spacing: the next byte is the advance
            length +%= @as(c_int, s[0]);
            s += 1;
        } else if (c == 254) {
            // decimal: the next byte is a number, its tens count six pixels each
            c = s[0];
            s += 1;
            if (c > 9) {
                length +%= @as(c_int, c / 10) *% 6;
            }
        } else if (c >= 248) {
            // a code with one parameter byte
            s += 1;
        }
    }
}
