//! Draws a chain counter's text, sliding in and out (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

const TextStatus = *const fn (?*const anyopaque, ?*const anyopaque) callconv(.c) void;

const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);

/// orig: func_80095A4C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Draws "1 CHAIN" or "N CHAINS" (N: the entry's unkC) at x, y, slid left by 64/16/4/1 over the entry's first
/// four frames (one chain only) and its last four (87..90).
pub fn func_80095A4C(x_arg: c_int, y: c_int, p: *v.D_800F1BAC_t) callconv(.c) void {
    var x = x_arg;
    var text: [16]u8 = undefined;
    const offsets = [_]i16{ 64, 16, 4, 1 };
    if (p.unkB < 4 and p.unkC == 1) {
        x -%= offsets[p.unkB];
    } else if (p.unkB >= 87) {
        x -%= offsets[@intCast(90 - @as(c_int, p.unkB))]; // the frame counts to 90: past it the C reads its stack
    }
    if (p.unkC == 1) {
        _ = c.sprintf(&text, @constCast("$1 CHAIN"));
    } else {
        _ = c.sprintf(&text, @constCast("$%d CHAINS"), @as(c_int, p.unkC));
    }
    // PC_TEXT_STATUS (ui_text: the status words' packets).
    const first = psx.get(scratch.unk0);
    const prims: ?*anyopaque = @ptrFromInt(@intFromPtr(psx.get(scratch.unk4)) -% 16);
    c.vs_battle_renderTextRawColor(&text, (x & 0xFFFF) | (y << 16), 0x1040A0, prims);
    if (psx.weak(TextStatus, "pc_text_status")) |status| status(first, psx.get(scratch.unk0));
}
