//! Movement check: runs a state's callback when it has reached its target or is clear of the floor (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

const Callback = *const fn (?*v.movementCheckState, c_int) callconv(.c) void;

/// orig: func_800E0918 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Runs the mode's callback on a movement state that has reached its target or cleared the floor; returns 1 when it did.
pub fn func_800E0918(state: ?*v.movementCheckState, action: c_int, mode: c_int) callconv(.c) c_int {
    var callback: Callback = undefined;
    if (mode != 0) {
        callback = @ptrCast(&c.func_800E4B2C);
    } else {
        callback = @ptrCast(&c.func_800E4B70);
    }
    const s = state.?;

    if ((@as(c_int, s.flags) & 6) != 0) {
        const distance: c_uint = c.func_800E45F4(psx.conv(c_uint, s.target), psx.conv(c_uint, s.position));
        if (mode != 0 and distance < 16) {
            if ((psx.get(s.object).?.flags & 0x200000) == 0) {
                callback(state, action);
                return 1;
            }
        } else if (v.g.D_800F58BC().*.?.unk14 != 0) {
            if ((c.func_800E42E0(psx.conv(c_uint, s.position)) +% @as(c_int, s.height)) < v.g.D_800F58BC().*.?.unk14) {
                if ((psx.get(s.object).?.flags & 0x200000) != 0) {
                    if (c.func_800D954C(@ptrCast(s), psx.conv(c_uint, s.position)) > 0) {
                        c.func_800E678C(@ptrCast(s));
                        c.func_800E4B10(s);
                        return 1;
                    }
                } else {
                    callback(state, action);
                    return 1;
                }
            }
        }
    }
    return 0;
}
