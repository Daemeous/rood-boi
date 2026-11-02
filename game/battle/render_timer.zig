//! Draws the puzzle timer (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: _renderTimer (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The timer's minutes, seconds and hundredths (the hundredths from the state flags; 59:59:99 once the top byte is set) as BCD digits into D_800EC258, drawn at (128, 200).
pub fn renderTimer(value: [*c]c_int) callconv(.c) void {
    var digits: [4]u8 = @bitCast(value.*);
    digits[0] = v.g.vs_main_stateFlags().puzzleTimerHundredths;
    if (digits[3] != 0) {
        digits[2] = 59;
        digits[1] = 59;
        digits[0] = 99;
    }
    const text = v.g.D_800EC258().*.?;
    var i: c_int = 0;
    while (i < 3) : (i += 1) { // i < 3
        const bcd = c.vs_battle_toBCD(digits[@intCast(i)]);
        text[@intCast(6 - i * 3)] = psx.conv(u8, (bcd >> 4) +% '0');
        text[@intCast(7 - i * 3)] = psx.conv(u8, (bcd & 0xF) +% '0');
    }
    const ot: ?*anyopaque = @ptrFromInt(@intFromPtr(psx.get(v.g.vs_scratch().unk4)) -% 4);
    c.vs_battle_renderTextRaw(text, (200 << 16) | (128 & 0xFFFF), ot);
}
