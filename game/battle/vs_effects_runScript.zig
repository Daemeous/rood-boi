//! Runs an effect script's commands (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_runScript (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs commands through vs_effects_scriptOpcodeHandlers while they return 1; 0 when one returns 2, else 1. An opcode past the table's 49 is reported (ROOD_PC: pc_effect_bad_opcode) and the script ends (the C called whatever the byte past the table held: the program's own data, a crash); the table is the original's, so this is a script whose cursor is not on a command (data read as one).
pub fn vs_effects_runScript(script: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    const Command = *const fn (?*v.vs_effects_EffectInstance) callconv(.c) c_int;
    var result: c_int = undefined;
    while (true) {
        const opcode: c_uint = c.vs_effects_readScriptByte(@ptrCast(script));
        if (opcode >= 49) {
            c.pc_effect_bad_opcode(@as(c_int, @intCast(opcode)), @as(c_int, script.?.cursor) -% 1);
            return 0;
        }
        const command: ?Command = @ptrCast(psx.at(v.g.vs_effects_scriptOpcodeHandlers(), opcode).*);
        result = command.?(script);
        if (result == 2) {
            return 0;
        }
        if (result != 1) break;
    }
    return 1;
}
