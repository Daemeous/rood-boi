//! Frees a block held by a battle record's unkD1C.unk30 pointer and clears the pointer
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_freeScriptTimeline (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Frees the heap block at arg0->context.unk30, if any, and clears the pointer; always returns 1.
pub fn vs_effects_freeScriptTimeline(arg0: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    if (psx.get(arg0.?.context.unk30)) |block| {
        c.vs_main_freeHeapR(@ptrCast(block));
        psx.put(&arg0.?.context.unk30, null);
    }
    return 1;
}
