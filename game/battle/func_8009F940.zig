//! Sets a battle record's flag byte and, when the flag is nonzero, copies two
//! records into it (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_8009F940 (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets the flag of the record at `index`; when `flag` is nonzero, copies `first` and `second` into it.
pub fn func_8009F940(index: c_int, flag: c_int, first: ?*v.D_800F4538_unk58_2, second: ?*v.D_800F4538_unk58_2) callconv(.c) void {
    const temp = psx.at(v.g.vs_actors_actorRecords(), index).*;
    if (temp) |t| {
        t.companionSlot = psx.conv(u8, flag);
        if (flag != 0) {
            @as(*align(1) c_int, @ptrCast(&t.unk6F4)).* = @as(*align(1) const c_int, @ptrCast(first.?)).*;
            t.unk6F8 = second.?.*;
        }
    }
}
