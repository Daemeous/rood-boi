//! Native versions of the original room-effect callback tables' entries (pc/src/room_callbacks.inc, in 2842C.c): each casts its arguments and calls the original handler.
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: pc_cb_80091C6C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_advanceRoomTextureFrames on the entry's object.
pub fn pc_cb_80091C6C(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    return c.vs_field_advanceRoomTextureFrames(@ptrCast(p));
}

/// orig: pc_cb_80091E10 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_scrollRoomTextureCoordinates on the entry's object.
pub fn pc_cb_80091E10(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    return c.vs_field_scrollRoomTextureCoordinates(@ptrCast(p));
}

/// orig: pc_cb_80091FE8 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_animateRoomTextureImages on the entry's object.
pub fn pc_cb_80091FE8(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    return c.vs_field_animateRoomTextureImages(@ptrCast(p));
}

/// orig: pc_cb_800923F8 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_updateRoomRotationEffect on the entry's object.
pub fn pc_cb_800923F8(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    return c.vs_field_updateRoomRotationEffect(@ptrCast(p));
}

/// orig: pc_cb_80092540 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092540, which takes no argument.
pub fn pc_cb_80092540(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    _ = p;
    return c.func_80092540();
}

/// orig: pc_cb_80092C68 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092C68 on the entry's two objects.
pub fn pc_cb_80092C68(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092C68(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092E14 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_setRoomEffectVelocity on the entry's two objects.
pub fn pc_cb_80092E14(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_setRoomEffectVelocity(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092F6C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092F6C on the entry's two objects.
pub fn pc_cb_80092F6C(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092F6C(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092F74 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_hideRoomEffectObject on the entry's two objects.
pub fn pc_cb_80092F74(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_hideRoomEffectObject(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092F98 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_executeRoomEffectShowGroup on the entry's two objects.
pub fn pc_cb_80092F98(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_executeRoomEffectShowGroup(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092FBC (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_advanceRoomEffectMotion on the entry's two objects.
pub fn pc_cb_80092FBC(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_advanceRoomEffectMotion(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_8009352C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_executeRoomEffectPlaySound on the entry's two objects.
pub fn pc_cb_8009352C(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_executeRoomEffectPlaySound(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_800935B8 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_executeRoomEffectStopSound on the entry's two objects.
pub fn pc_cb_800935B8(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_executeRoomEffectStopSound(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092E7C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_setRoomEffectTransform on the entry's two objects.
pub fn pc_cb_80092E7C(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_setRoomEffectTransform(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_800935F8 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_executeRoomEffectStartEffect on the entry's two objects.
pub fn pc_cb_800935F8(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_executeRoomEffectStartEffect(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_8009361C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_executeRoomEffectHideGroup on the entry's two objects.
pub fn pc_cb_8009361C(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_executeRoomEffectHideGroup(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_800936F0 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_800936F0 on the entry's two objects.
pub fn pc_cb_800936F0(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_800936F0(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80093640 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs vs_field_executeRoomEffectStopScript on the entry's two objects.
pub fn pc_cb_80093640(a: ?*v.func_80092F74_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.vs_field_executeRoomEffectStopScript(@ptrCast(a), @ptrCast(@alignCast(b)));
}
