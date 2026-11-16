//! Initialises a battle actor slot from its character init data and its flags
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_initializeActorFromTemplate (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets up battle actor `id` from its init data, its allegiance flags and its stats, then calls vs_actors_applyActorInitialStats.
pub fn vs_actors_initializeActorFromTemplate(id: c_int, arg1: ?*v.vs_battle_charInitData, arg2: c_int, arg3: c_int, arg4: c_int, arg5: c_int) callconv(.c) void {
    const actor = psx.at(v.g.vs_battle_actors(), id).*.?;
    actor.id = id;
    const temp_s4 = psx.get(actor.stats);
    actor.unk18 = arg1.?.unk2;
    actor.subType = arg1.?.subType;
    actor.unk2A = psx.conv(u8, arg2);
    actor.unk2B = psx.conv(u8, arg3);
    actor.weaponDrawn = 0;

    switch (id) {
        0 => {
            actor.allegiance = 1;
        },
        1 => {
            actor.allegiance = 2;
        },
        else => switch (arg4 & 3) {
            1 => {
                actor.allegiance = 4;
                actor.weaponDrawn |= 1;
                _ = c.vs_actors_setActorWeaponDrawn(id, 1, 0);
            },
            2, 3 => {
                actor.allegiance = 8;
            },
            0 => {
                actor.allegiance = 2;
            },
            else => {},
        },
    }

    actor.unk26 = if ((arg4 & 0x80) != 0) 1 else 0;
    actor.unk24 = psx.conv(u8, arg4 & 0x1C);
    actor.unk8 = 0;
    actor.unk2C.u8 = .{ 0, 0, 0, 0 };
    c.vs_actors_applyActorInitialStats(id, @ptrCast(temp_s4), @ptrCast(arg1), arg5);
}
