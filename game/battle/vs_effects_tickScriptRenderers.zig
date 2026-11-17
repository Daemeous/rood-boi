//! An effect-script opcode: steps the script's effects (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickScriptRenderers (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls each effect's renderer with 2; frees (vs_effects_detachEffectRenderer) those that return 0. Returns 1.
pub fn vs_effects_tickScriptRenderers(script: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    const Renderer = *const fn (*align(4) v.func_800D4910_t, c_uint, c_int) callconv(.c) c_int; // effectExec
    var node = psx.get(script.?.context.unk34);
    while (node) |n| {
        const next = n.next;
        const renderer: ?Renderer = @ptrCast(n.renderer);
        if (renderer.?(n, 2, 0) == 0) {
            c.vs_effects_detachEffectRenderer(@ptrCast(script), @ptrCast(@alignCast(n)));
        }
        node = next;
    }
    return 1;
}
