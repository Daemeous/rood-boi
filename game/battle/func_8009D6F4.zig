//! Clears the battle flags and the scratch block of every live actor slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009D6F4 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Resets the flags and the scratch block of every non-empty actor slot.
pub fn func_8009D6F4() callconv(.c) void {
    const actors = v.g.D_800F4538();
    var i: c_int = 0;
    while (i < 17) : (i +%= 1) {
        const actor = actors[@intCast(i)] orelse continue;

        if (psx.getBits(actor, "unk5AC_3") != 0) {
            psx.setBits(actor, "unk5AC_3", 0);
        }

        actor.unk0.unk11 = 0;
        psx.setBits(actor, "unk5AC_20", 0);
        psx.setBits(&actor.unk0, "unkC_0", 0);
        psx.setBits(&actor.unk0, "unkC_4", 0);
        psx.setBits(&actor.unk0, "unkA_3", 0);
        psx.setBits(&actor.unk0, "unk9_6", 0);
        psx.setBits(actor, "unk5AC_15", 0);
        psx.setBits(&actor.unk0, "unkA_0", 0);

        _ = c.memset(
            @as(?*anyopaque, @ptrCast(&actor.unk1848)),
            @as(c_int, 0),
            @as(usize, @sizeOf(v.D_800F4538_unk1848)),
        );
    }
}
