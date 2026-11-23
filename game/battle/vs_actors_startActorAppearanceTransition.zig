//! Sets the mode nibble of battle entity vs_actors_actorRecords[arg0] from arg1, then
//! follows its chain link unk17FD (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_startActorAppearanceTransition (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Sets entity `index`'s mode nibble from `mode` (0, 1 or 2), then recurses along its chain link.
pub fn vs_actors_startActorAppearanceTransition(arg0: c_int, arg1: c_int) callconv(.c) void {
    const temp_a0: *v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), arg0).*.?;

    switch (arg1) {
        0 => psx.setBits(&temp_a0.unk0, "unk9_0", 14),
        1 => psx.setBits(&temp_a0.unk0, "unk9_0", 6),
        2 => psx.setBits(&temp_a0.unk0, "unk9_0", 9),
        else => {},
    }
    if (@as(c_int, temp_a0.companionSlot) > 1) {
        c.vs_actors_startActorAppearanceTransition(@as(c_int, temp_a0.companionSlot), arg1);
    }
}
