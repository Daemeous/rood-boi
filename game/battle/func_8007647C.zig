//! Clears a battle slot's stats block and sets its flags (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007647C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the stats of battle slot `slot` and copies the name from D_8004FDD0 and the first row of the arts table; `mode` picks its flag bit.
pub fn func_8007647C(arg0: c_int, arg1: c_int) callconv(.c) void {
    const temp_s1 = psx.at(v.g.vs_battle_actors(), arg0).*.?;
    const temp_s0 = psx.get(temp_s1.stats).?;

    c.vs_main_bzero(temp_s0, @sizeOf(v.vs_battle_actor2));

    temp_s0.maxHP = 1;
    temp_s0.currentHP = 1;

    // The name is 24 bytes. The original copied 24 from D_8004FDD0, a 20-byte table, so the last four were the first four bytes
    // of the next table in the executable, vs_main_artsPointsRequirements: the first row's first two words (10 and 65 kills).
    // Both parts are named here, so nothing depends on the two tables lying side by side.
    const name = &psx.get(temp_s1.stats).?.name;
    var i: usize = 0;
    while (i < 20) : (i += 1) name[i] = v.g.D_8004FDD0()[i];
    const arts_row: [*]const u8 = @ptrCast(v.g.vs_main_artsPointsRequirements());
    while (i < 24) : (i += 1) name[i] = @bitCast(arts_row[i - 20]);

    switch (@as(c_int, arg1) & 0xFF) {
        1 => {
            psx.setBits(temp_s0, "unk956_0", 1);
            c.vs_battle_setActorMarker(arg0, 8);
        },
        2, 4 => {
            psx.setBits(temp_s0, "unk956_0", 0);
        },
        else => {
            psx.setBits(temp_s0, "unk956_0", 1);
        },
    }
    temp_s1.unk27 = 0x80;
    temp_s1.unk29 = 0;
    temp_s1.defeated = 0;
    temp_s0.actorId = 0x80;
}
