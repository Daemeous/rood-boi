//! The next byte of an effect script (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_readScriptByte (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the script byte at the cursor unkA and advances the cursor (a u16: it wraps).
pub fn vs_effects_readScriptByte(script: ?*v.vs_effects_EffectInstance) callconv(.c) u8 {
    const s = script.?;
    const cursor = s.cursor;
    s.cursor = cursor +% 1;
    return @as([*]u8, @ptrCast(psx.get(s.unkC).?))[cursor];
}
