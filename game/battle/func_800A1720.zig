//! Reads the vertical position of a battle actor's model slot (src/BATTLE/BATTLE.PRG/38C1C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A1720 (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Runs the slot query with the selector mapped to 0 for 0xFD, then stores the slot's vertical position in arg2[6] for 0xFD.
pub fn func_800A1720(arg0: c_int, arg1: c_int, arg2: [*c]c_int, arg3: [*c]c_int) callconv(.c) c_int {
    const selector: c_int = if (arg1 == 0xFD) 0 else arg1;

    const result = c.func_800A17BC(arg0, selector, @ptrCast(arg2), arg3);

    if (result < 0) {
        return result;
    }

    if (arg1 == 0xFD) {
        const slot: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*;
        // ROOD_PC: empty slot, read PS1 address 0 (at 0x80000000) as func_800A17BC does.
        if (slot == null) {
            arg2[6] = @as(c_int, psx.lowRam(v.D_800F4538_t).unk0.position.vy);
            return 0;
        }

        arg2[6] = @as(c_int, slot.?.unk0.position.vy);
    }

    return 0;
}
