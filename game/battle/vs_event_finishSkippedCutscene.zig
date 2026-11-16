//! The script skip's tail (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_finishSkippedCutscene (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the three vs_event_skipPaletteResetCommand commands, then walks the script by the opcode lengths until the
/// D_800E9BE8 marker (or the end opcode 0x0F, which also calls vs_field_restartRoomEffectSound); clears flag 0xA8.
pub fn vs_event_finishSkippedCutscene(script_arg: [*c]u8) callconv(.c) void {
    var script = script_arg;
    var i: i16 = 0;
    while (i < 3) : (i += 1) { // i < 3: no overflow
        v.g.vs_event_skipPaletteResetCommand()[1] = psx.conv(u8, (@as(c_int, i) << 4) | 8);
        _ = c.func_800B9C58(v.g.vs_event_skipPaletteResetCommand(), 0);
    }
    c.func_800AACDC();
    i = 0xF;
    while (script[0] != i) {
        if (c.memcmp(script, v.g.D_800E9BE8(), @as(c_int, 4)) == 0) {
            c.vs_battle_setStateFlag(0xA8, 0);
            return;
        }
        script += v.g.vs_event_opcodeSizes()[script[0]];
    }
    c.vs_field_restartRoomEffectSound();
    c.vs_battle_setStateFlag(0xA8, 0);
}
