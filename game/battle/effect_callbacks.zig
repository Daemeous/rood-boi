//! The effect command table's entries (generated effect_callbacks.inc, in 5BF94.c): each calls its command on the node.
const c = @import("c_5BF94");

/// orig: pc_effect_func_800D4720 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_yieldScriptAt on the node.
pub fn pc_effect_func_800D4720(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_yieldScriptAt(@ptrCast(node));
}

/// orig: pc_effect_func_800D474C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_jumpScript on the node.
pub fn pc_effect_func_800D474C(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_jumpScript(@ptrCast(node));
}

/// orig: pc_effect_func_800D4778 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D4778 on the node.
pub fn pc_effect_func_800D4778(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D4778(@ptrCast(node));
}

/// orig: pc_effect_func_800D47C0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_endScript on the node.
pub fn pc_effect_func_800D47C0(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_endScript(@ptrCast(node));
}

/// orig: pc_effect_func_800D47C8 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D47C8 on the node.
pub fn pc_effect_func_800D47C8(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D47C8(@ptrCast(node));
}

/// orig: pc_effect_func_800D47F4 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_attachScriptRenderer on the node.
pub fn pc_effect_func_800D47F4(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_attachScriptRenderer(@ptrCast(node));
}

/// orig: pc_effect_func_800D487C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_branchIfRenderersFinished on the node.
pub fn pc_effect_func_800D487C(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_branchIfRenderersFinished(@ptrCast(node));
}

/// orig: pc_effect_func_800D48A4 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_loadTimelineImmediate on the node.
pub fn pc_effect_func_800D48A4(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_loadTimelineImmediate(@ptrCast(node));
}

/// orig: pc_effect_func_800D48E4 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D48E4 on the node.
pub fn pc_effect_func_800D48E4(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D48E4(@ptrCast(node));
}

/// orig: pc_effect_func_800D5150 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D5150 on the node.
pub fn pc_effect_func_800D5150(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D5150(@ptrCast(node));
}

/// orig: pc_effect_func_800D4910 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_tickScriptRenderers on the node.
pub fn pc_effect_func_800D4910(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_tickScriptRenderers(@ptrCast(node));
}

/// orig: pc_effect_func_800D4984 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_tickScriptTransforms on the node.
pub fn pc_effect_func_800D4984(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_tickScriptTransforms(@ptrCast(node));
}

/// orig: pc_effect_func_800D49A4 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_setActionEffectVerticalOffset on the node.
pub fn pc_effect_func_800D49A4(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_setActionEffectVerticalOffset(@ptrCast(node));
}

/// orig: pc_effect_func_800D4A94 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_initializeScriptTransforms on the node.
pub fn pc_effect_func_800D4A94(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_initializeScriptTransforms(@ptrCast(node));
}

/// orig: pc_effect_func_800D4AB4 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D4AB4 on the node.
pub fn pc_effect_func_800D4AB4(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D4AB4(@ptrCast(node));
}

/// orig: pc_effect_func_800D4AEC (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_setScriptRegister on the node.
pub fn pc_effect_func_800D4AEC(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_setScriptRegister(@ptrCast(node));
}

/// orig: pc_effect_func_800D4B34 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_branchIfScriptRegisterEqual on the node.
pub fn pc_effect_func_800D4B34(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_branchIfScriptRegisterEqual(@ptrCast(node));
}

/// orig: pc_effect_func_800D4B90 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_branchIfParticleCountEqual on the node.
pub fn pc_effect_func_800D4B90(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_branchIfParticleCountEqual(@ptrCast(node));
}

/// orig: pc_effect_func_800D4BD0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_emitRegisterParticles on the node.
pub fn pc_effect_func_800D4BD0(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_emitRegisterParticles(@ptrCast(node));
}

/// orig: pc_effect_func_800D4C18 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_tickEffectParticles on the node.
pub fn pc_effect_func_800D4C18(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_tickEffectParticles(@ptrCast(node));
}

/// orig: pc_effect_func_800D4D44 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_refreshParticleEnvironment on the node.
pub fn pc_effect_func_800D4D44(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_refreshParticleEnvironment(@ptrCast(node));
}

/// orig: pc_effect_func_800D4D64 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D4D64 on the node.
pub fn pc_effect_func_800D4D64(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D4D64(@ptrCast(node));
}

/// orig: pc_effect_func_800D4D98 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D4D98 on the node.
pub fn pc_effect_func_800D4D98(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D4D98(@ptrCast(node));
}

/// orig: pc_effect_func_800D4DA8 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_continueScript on the node.
pub fn pc_effect_func_800D4DA8(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_continueScript(@ptrCast(node));
}

/// orig: pc_effect_func_800D4DB0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_loadRegisterTimeline on the node.
pub fn pc_effect_func_800D4DB0(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_loadRegisterTimeline(@ptrCast(node));
}

/// orig: pc_effect_func_800D4E5C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_tickTimelineFromScript on the node.
pub fn pc_effect_func_800D4E5C(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_tickTimelineFromScript(@ptrCast(node));
}

/// orig: pc_effect_func_800D4E90 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_branchIfTimelineFinished on the node.
pub fn pc_effect_func_800D4E90(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_branchIfTimelineFinished(@ptrCast(node));
}

/// orig: pc_effect_func_800D4EC0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_freeScriptTimeline on the node.
pub fn pc_effect_func_800D4EC0(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_freeScriptTimeline(@ptrCast(node));
}

/// orig: pc_effect_func_800D4F00 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_setParticleColorCurves on the node.
pub fn pc_effect_func_800D4F00(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_setParticleColorCurves(@ptrCast(node));
}

/// orig: pc_effect_func_800D4FB4 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D4FB4 on the node.
pub fn pc_effect_func_800D4FB4(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D4FB4(@ptrCast(node));
}

/// orig: pc_effect_func_800D5048 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs vs_effects_completeChildScript on the node.
pub fn pc_effect_func_800D5048(node: ?*anyopaque) callconv(.c) c_int {
    return c.vs_effects_completeChildScript(@ptrCast(node));
}

/// orig: pc_effect_func_800D5088 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Effect command table entry: runs func_800D5088 on the node.
pub fn pc_effect_func_800D5088(node: ?*anyopaque) callconv(.c) c_int {
    return c.func_800D5088(@ptrCast(node));
}
