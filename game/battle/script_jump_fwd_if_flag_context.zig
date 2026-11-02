//! Script opcode: jump forward when the context flag matches (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_jumpFwdIfFlagContext (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Jumps to the script offset in the operand when the context flag equals the script's flag byte, else returns 0.
pub fn script_jumpFwdIfFlagContext(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    if (v.g.D_800F4BB4().* == arg0[1]) {
        const off: c_short = c.vs_battle_getShort(arg0 + 2);
        return @bitCast(psx.addr(psx.at(arg0, off)));
    }
    return 0;
}
