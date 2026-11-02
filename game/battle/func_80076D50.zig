//! Sets an actor slot's defaults (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80076D50 (src/BATTLE/BATTLE.PRG/146C.c)
/// Actor `id` gets its id, unk18 (arg1), unk2A/unk2B (arg2, arg3) and allegiance: 1 for the player, 2 for slot 1,
/// else from arg4's low bits (1: 4 with the weapon drawn, 0: 2, 2 or 3: 8); unk24 from arg4's bits 2..4; its stats
/// 100 HP and MP, no risk, the unk30..unk33 pairs (16/32 for unk18 41, 2/8 for 48, else 2/4; 9/18) and range 5/4/5.
pub fn func_80076D50(id: c_uint, arg1: c_int, arg2: c_int, arg3: c_int, arg4: c_int) callconv(.c) void {
    const actor = v.g.vs_battle_actors()[id].?;
    const stats = psx.get(actor.stats).?;
    actor.id = @bitCast(id);
    actor.unk18 = psx.conv(u16, arg1);
    actor.subType = 0;
    actor.unk2A = psx.conv(u8, arg2);
    actor.unk2B = psx.conv(u8, arg3);
    actor.weaponDrawn = 0;
    switch (id) {
        0 => actor.allegiance = 1,
        1 => actor.allegiance = 2,
        else => switch (arg4 & 3) {
            1 => {
                actor.allegiance = 4;
                actor.weaponDrawn |= 1;
                c.func_8009D934(@bitCast(id), 1, 0);
            },
            0 => actor.allegiance = 2,
            2, 3 => actor.allegiance = 8,
            else => unreachable, // arg4 & 3
        },
    }
    actor.unk24 = psx.conv(u8, arg4 & 0x1C);
    actor.unk2C.u8[3] = 0;
    actor.unk2C.u8[2] = 0;
    actor.unk2C.u8[1] = 0;
    actor.unk2C.u8[0] = 0;
    actor.unk1E = 0;
    actor.unk8 = 0;
    stats.maxHP = 100;
    stats.currentHP = 100;
    stats.maxMP = 100;
    stats.currentMP = 100;
    stats.risk = 0;
    stats.unk954 = 0;
    if (arg1 == 41) {
        stats.unk30 = 16;
        stats.unk32 = 32;
    } else if (arg1 == 48) {
        stats.unk30 = 2;
        stats.unk32 = 8;
    } else {
        stats.unk30 = 2;
        stats.unk32 = 4;
    }
    stats.unk31 = 9;
    stats.unk33 = 18;
    stats.weapon.unk10B = 20;
    stats.weapon.range.range = 5;
    stats.weapon.range.unk1 = 4;
    stats.weapon.range.unk2 = 5;
    psx.setBits(stats, "unk956_0", 0);
    psx.setBits(&stats.weapon.range, "attackShapeAngle", 0);
    actor.unk27 = 128;
    actor.unk29 = 0;
    actor.defeated = 0;
    stats.unk957 = 128;
}
