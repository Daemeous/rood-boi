//! Queues a battle ability timing result display (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_battle_renderBattleAbilityTimingResult (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Clears any queued timing result of the same kind, then queues a new one for `arg0`.
pub fn renderBattleAbilityTimingResult(arg0: c_int) callconv(.c) c_int {
    const first: ?*anyopaque = v.g.vs_battle_floatingLabels().*;
    const base: [*c]v.D_800F1BAC_t = @ptrCast(@alignCast(first));

    // The C re-reads the count each pass and bumps the pointer with the index.
    var i: c_int = 0;
    while (i < @as(c_int, v.g.vs_battle_floatingLabelCount().*)) : (i +%= 1) {
        const entry = psx.at(base, i);
        if (entry.unk8.u16 == 4) {
            c.vs_battle_removeFloatingLabel(i);
            break;
        }
    }

    const raw: ?*anyopaque = @ptrCast(c.vs_battle_reserveFloatingLabel());
    const q: *v.D_800F1BAC_t = @ptrCast(@alignCast(raw.?));
    q.unkB = 0;
    q.unk8.u8[0] = 4;
    q.unk8.u8[1] = 0;
    q.unkC = psx.conv(u16, arg0);
    q.displayTextLen = psx.conv(u8, c.rand() & 7);
    v.g.vs_battle_floatingLabelCount().* +%= 1;
    return 0;
}
