//! Scans the directional cache around an actor for the lowest height among its
//! directions (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E0850 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Returns the lowest height sampled at the offsets of the directions selected by `arg1`, or 0.
pub fn func_800E0850(arg0: ?*v.func_800E0850_t, arg1: c_uint) callconv(.c) c_int {
    var min: c_int = 0;
    var count: c_int = 1;

    if ((arg1 & 0x3FF) != 0) {
        count = 3;
    }
    var slot: c_uint = (arg1 >> 9) & 6;
    while (true) {
        count -%= 1;
        if (count == -1) break;
        if (c.func_800E0678(@ptrCast(arg0), psx.conv(c_int, slot)) != 0) {
            // Scratchpad direction table: one byte per slot, dx and dz are 2-bit signed fields.
            const entry: *v.D_800F16EC_t = @ptrFromInt(0x1F8003EC + @as(usize, slot));
            const h = c.func_800E42EC(
                @as(c_int, arg0.?.unk34) +% @as(c_int, psx.getBits(entry, "dx")),
                @as(c_int, arg0.?.unk36) +% @as(c_int, psx.getBits(entry, "dz")),
            );
            if (h < min) {
                min = h;
            }
        }
        slot = (slot +% 1) & 7;
    }
    return min;
}
