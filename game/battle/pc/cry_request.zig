//! Per-slot cry/animation request (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const psx = @import("common").psx;
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");

/// orig: func_800E4180 (pc/src/battle_movement.c)
/// Per-slot cry/animation request (0x474 + slot * 2: count << 8 | kind). A
/// new request (kind != 3) fires with chance/128 and loads its length from
/// D_800F1758; each call counts down, sets 0x13A = 0x40 and, for kinds 1 and
/// 2, a pitch at 0x188 by remaining length before vs_battle_aiIssueActorCommand(16). Returns
/// 1 while at least 2 frames remained.
pub fn requestCry(state: *ai.vs_battle_aiState, chance: c_int, kind: c_int, slot: c_int) callconv(.c) c_int {
    const entry = psx.fieldAt(u16, &state.requests, @as(isize, slot *% 2));
    var count: c_int = (entry.* >> 8) & 0xff;
    var stored: c_int = entry.* & 0xff;
    if (count == 0) {
        if (kind == 3) return 0;
        if (chance < c.vs_battle_aiNextActorRandomMasked(0x80)) return 0;
        count = psx.at(v.g.D_800F1758(), kind).*;
        stored = kind;
    }
    entry.* = psx.conv(u16, ((count -% 1) << 8) | stored);
    state.timer13A = 0x40;
    var pitch: c_int = undefined;
    if (kind == 2) {
        pitch = if (count < 0xb) 0 else 0x500;
    } else if (kind == 1) {
        pitch = if (count >= 0x35) 0x320 else if (count >= 0x12) -0x320 else 0;
    } else {
        return 0;
    }
    state.heading = psx.conv(i16, pitch);
    c.vs_battle_aiIssueActorCommand(state, 16);
    return @intFromBool(count >= 2);
}
