//! Fills an attack-hit record from an action, the attacking input and the hit
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_battle_populateActionTargetPreview (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills the hit record `out` from the action `input` and the hit `hit`.
pub fn vs_battle_populateActionTargetPreview(out: ?*v.func_8006F630_t1, input: ?*v.func_8006F630_t2, hit: ?*v.func_8006F630_t3) callconv(.c) void {
    const dst = out.?;
    const src = input.?;
    const h = hit.?;

    // vs_main_actions[actionId]: the C does not check actionId against 256.
    const action = psx.at(v.g.actions(), src.actionId);
    const effect: c_int = @as(c_int, psx.getBits(&action.hitParams[0], "effect"));

    switch (effect) {
        0x3A, 0x3E => {
            dst.effect = psx.conv(u8, effect);
            dst.unkC = h.unk4;
        },
        0x3B, 0x3F => {
            dst.effect = psx.conv(u8, effect);
            dst.unkC = h.unk6;
        },
        else => dst.effect = psx.conv(u8, effect),
    }

    dst.unkE = psx.conv(i16, h.unk2);
    dst.unk10 = h.unk14 & 0x1FFFFFE0;
    dst.unk14 = @as(u8, psx.getBits(action, "unk2_4"));
    dst.unk15 = src.unk3;
    dst.unk16 = src.unk2;

    // vs_battle_actors[actorId] and its stats are dereferenced unconditionally in the C.
    const actor = v.g.vs_battle_actors()[psx.getBits(h, "actorId")].?;
    const stats = psx.get(actor.stats).?;
    dst.enemyClass = @as(u8, psx.getBits(stats, "enemyClass"));
}
