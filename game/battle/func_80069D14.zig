//! Marks battle actors flagged 0x80 with unk9 0x18 by setting their unk8 to 8
//! (src/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: func_80069D14 (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets unk8 to 8 on each battle actor in slots 0..15 whose unk8 is 0x80 and unk9 is 0x18.
pub fn func_80069D14() callconv(.c) void {
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        const actor = v.g.vs_battle_actors()[@intCast(i)];

        if (actor != null and actor.?.unk8 == 0x80 and v.g.vs_battle_actors()[@intCast(i)].?.unk9 == 0x18) {
            actor.?.unk8 = 8;
        }
    }
}
