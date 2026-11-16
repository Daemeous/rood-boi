//! Draws an ability's timing result word, sliding in and out (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

const TextStatus = *const fn (?*const anyopaque, ?*const anyopaque) callconv(.c) void;


/// The C's local tables (its string literals: not game text, 2842C has no vsstring block).
const timing = [_][*:0]const u8{ "$TOO   FAST!", "$FAST!", "", "$SLOW!", "$TOO   SLOW!" };
const success = [_][*:0]const u8{ "$COOL!", "$GOOD!", "$EXCELLENT!", "$RIGHT   ON!", "$PERFECT!", "$WELL   TIMED!", "$NICE!", "$GREAT!" };
const offsets = [_]i16{ 64, 16, 4, 1 };
const colors = [_]c_int{ 0x808000, 0x608020, 0x408040, 0x208060, 0x8080 };

/// orig: vs_battle_drawAbilityTimingResult (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Draws the entry's timing word (unkC: too fast .. too slow, each in its colour; 2: on time, the praise word
/// displayTextLen picks) at x, y, slid left by 64/16/4/1 over its first four frames and its last four (42..45).
pub fn vs_battle_drawAbilityTimingResult(x_arg: c_int, y: c_int, p: *v.D_800F1BAC_t) callconv(.c) void {
    var x = x_arg;
    draw: {
        var step: c_int = undefined;
        if (p.unkB < 4) {
            step = p.unkB;
        } else if (p.unkB >= 42) {
            step = 45 - @as(c_int, p.unkB);
        } else {
            break :draw;
        }
        x -%= offsets[@intCast(step)]; // the frame counts to 45: past it the C reads its stack
    }
    // PC_TEXT_STATUS (ui_text: the status words' packets). unkC is 0..4 and displayTextLen 0..7 here
    // (the result and the praise drawn for it): past them the C reads its stack as the text.
    const first = psx.get(common.scratch.get().unk0);
    const prims: ?*anyopaque = @ptrFromInt(@intFromPtr(psx.get(common.scratch.get().unk4)) -% 16);
    if (p.unkC != 2) {
        c.vs_battle_renderTextRawColor(timing[p.unkC], (x & 0xFFFF) | (y << 16), colors[p.unkC], prims);
    } else {
        c.vs_battle_renderTextRawColor(success[p.displayTextLen], (x & 0xFFFF) | (y << 16), 0x408040, prims);
    }
    if (psx.weak(TextStatus, "pc_text_status")) |status| status(first, psx.get(common.scratch.get().unk0));
}
