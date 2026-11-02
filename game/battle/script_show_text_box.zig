//! Script opcode: opens text box script[1] with the string at script[2], or closes it and records its selection (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_showTextBox (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opens text box script[1] on the string at script[2] (a hold flag in script[3]), or closes it and records its selection.
pub fn script_showTextBox(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const statuses = v.g.vs_battle_textBoxStatuses();
    if (statuses[script[1]] == 0) {
        // D_800F4BB0 is a pointer global: the inner read dereferences it, so a NULL table is the C's crash.
        const table = v.g.D_800F4BB0().*.?;
        const index = psx.at(table, script[2]).*;
        _ = c.vs_battle_setTextBox(@as(c_int, script[1]), @ptrCast(psx.at(table, index)));
        if (script[3] == 0) {
            statuses[script[1]] = 1;
            return 1;
        }
        return 0;
    }

    const selection = c.vs_battle_setTextBox(@as(c_int, script[1]), null);
    if (selection >= 0) {
        if (selection > 0) {
            v.g.vs_main_stateFlags().textBoxSelection = psx.conv(u8, selection);
        }
        statuses[script[1]] = 0;
        return 0;
    }
    return 1;
}
