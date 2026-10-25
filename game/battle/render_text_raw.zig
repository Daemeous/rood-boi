//! Text rendering entry with the default colour (src/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: vs_battle_renderTextRaw (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Renders raw text at `xy` in the default grey (128, 128, 128).
pub fn renderTextRaw(text: [*c]const u8, xy: c_int, nextPrim: ?*anyopaque) callconv(.c) void {
    // vs_getRGB(128, 128, 128): (0 << 24) | (128 << 16) | (128 << 8) | 128.
    const color: c_int = (@as(c_int, 128) << 16) | (@as(c_int, 128) << 8) | 128;
    c.vs_battle_renderTextRawColor(text, xy, color, nextPrim);
}
