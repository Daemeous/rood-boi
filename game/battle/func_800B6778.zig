//! The script skip's tail (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B6778 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the three D_800E9BE0 commands, then walks the script by the opcode lengths until the
/// D_800E9BE8 marker (or the end opcode 0x0F, which also calls func_80093A70); clears flag 0xA8.
pub fn func_800B6778(script_arg: [*c]u8) callconv(.c) void {
    var script = script_arg;
    var i: i16 = 0;
    while (i < 3) : (i += 1) { // i < 3: no overflow
        v.g.D_800E9BE0()[1] = psx.conv(u8, (@as(c_int, i) << 4) | 8);
        _ = c.func_800B9C58(v.g.D_800E9BE0(), 0);
    }
    c.func_800AACDC();
    i = 0xF;
    while (script[0] != i) {
        if (c.memcmp(script, v.g.D_800E9BE8(), @as(c_int, 4)) == 0) {
            c.vs_battle_setStateFlag(0xA8, 0);
            return;
        }
        script += v.g.D_800E9C30()[script[0]];
    }
    c.func_80093A70();
    c.vs_battle_setStateFlag(0xA8, 0);
}
