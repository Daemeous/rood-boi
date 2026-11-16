//! Frees each entry of the battle's linked list and its attached block (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_clearPrimaryEffects (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Walks the battle's entry list, freeing each entry's attached block and then the entry.
pub fn vs_effects_clearPrimaryEffects() callconv(.c) void {
    var cur: ?*v.D_800F53B8_t = v.g.vs_effects_primaryInstances().*;
    _ = c.vs_input_stopVibrationPatterns(0, 1);

    while (cur) |s0| {
        const next = psx.get(s0.next);
        if (psx.get(s0.context.unk30)) |block| {
            c.vs_main_freeHeapR(block);
            psx.put(&s0.context.unk30, null);
        }
        c.vs_effects_destroyPrimaryEffectInstance(@ptrCast(s0));
        cur = next;
    }
}
