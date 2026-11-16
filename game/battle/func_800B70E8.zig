//! An event script opcode that starts a vs_event_splashTweens slot's move (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: func_800B70E8 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Starts slot script[5] >> 6's move from its vs_render_gimLayers entry toward the script's targets; returns 1
/// while the slot is still busy, 0 otherwise (or with no vs_render_gimLayers).
pub fn func_800B70E8(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const base = v.g.vs_render_gimLayers().*;
    const k: usize = script[5] >> 6;
    const slot = &v.g.vs_event_splashTweens()[k];
    const from_base = base orelse return 0;
    const from: *v.D_800EB9BC_t = &@as([*]v.D_800EB9BC_t, @ptrCast(from_base))[k];
    if (slot.durationFrames != 0) return 1;
    slot.startX = from.unk8;
    slot.deltaX = psx.conv(i16, @as(c_int, psx.conv(i8, script[1])) *% 2 -% slot.startX);
    slot.startY = from.unkA;
    slot.deltaY = psx.conv(i16, @as(c_int, psx.conv(i8, script[2])) *% 2 -% slot.startY);
    slot.startScaleFixed12 = from.unkC;
    slot.deltaScaleFixed12 = psx.conv(i16, (@as(c_int, script[3]) << 7) -% slot.startScaleFixed12);
    slot.startBrightness = @as([*]u8, @ptrCast(from))[3];
    slot.deltaBrightness = psx.conv(i16, @as(c_int, script[4]) -% slot.startBrightness);
    slot.easingMode = script[5] & 7;
    slot.durationFrames = script[6];
    slot.elapsedFrames = 0;
    return 0;
}
