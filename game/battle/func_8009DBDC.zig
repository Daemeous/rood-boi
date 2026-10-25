//! Runs func_800AE6C0 on the actor in an enemy slot, with the per-animation table entry plus an offset (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009DBDC (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Calls func_800AE6C0 for the actor in `slot`, passing its table entry plus `offset`, when the slot is occupied.
pub fn func_8009DBDC(slot: c_int, offset: c_int) callconv(.c) void {
    // slot is not bounded by the C: psx.at indexes the pointer array as C does.
    const temp = psx.at(v.g.D_800F4538(), slot).*;
    if (temp) |actor| {
        const table_entry = v.g.D_800E9090()[actor.unk5B1];
        c.func_800AE6C0(@ptrCast(actor), @as(c_int, table_entry) +% offset, 0);
    }
}
