//! Runs an effect script's commands (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D51D8 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs commands through D_800EC3F4 while they return 1; 0 when one returns 2, else 1. An opcode past the table's 49 is reported (ROOD_PC: pc_unimplemented) and still called, as in the C.
pub fn func_800D51D8(script: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const Command = *const fn (?*v.D_800F53B8_t) callconv(.c) c_int;
    var result: c_int = undefined;
    while (true) {
        const opcode: c_uint = c.func_800D5170(@ptrCast(script));
        if (opcode >= 49) {
            c.pc_unimplemented("effect command opcode out of range");
        }
        const command: ?Command = @ptrCast(psx.at(v.g.D_800EC3F4(), opcode).*);
        result = command.?(script);
        if (result == 2) {
            return 0;
        }
        if (result != 1) break;
    }
    return 1;
}
