//! Native versions of the original room-effect callback tables' entries (pc/src/room_callbacks.inc, in 2842C.c): each casts its arguments and calls the original handler.
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: pc_cb_80091C6C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80091C6C on the entry's object.
pub fn pc_cb_80091C6C(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    return c.func_80091C6C(@ptrCast(p));
}

/// orig: pc_cb_80091E10 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80091E10 on the entry's object.
pub fn pc_cb_80091E10(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    return c.func_80091E10(@ptrCast(p));
}

/// orig: pc_cb_80091FE8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80091FE8 on the entry's object.
pub fn pc_cb_80091FE8(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    return c.func_80091FE8(@ptrCast(p));
}

/// orig: pc_cb_800923F8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_800923F8 on the entry's object.
pub fn pc_cb_800923F8(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    return c.func_800923F8(@ptrCast(p));
}

/// orig: pc_cb_80092540 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092540, which takes no argument.
pub fn pc_cb_80092540(p: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    _ = p;
    return c.func_80092540();
}

/// orig: pc_cb_80092C68 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092C68 on the entry's two objects.
pub fn pc_cb_80092C68(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092C68(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092E14 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092E14 on the entry's two objects.
pub fn pc_cb_80092E14(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092E14(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092F6C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092F6C on the entry's two objects.
pub fn pc_cb_80092F6C(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092F6C(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092F74 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092F74 on the entry's two objects.
pub fn pc_cb_80092F74(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092F74(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092F98 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092F98 on the entry's two objects.
pub fn pc_cb_80092F98(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092F98(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092FBC (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092FBC on the entry's two objects.
pub fn pc_cb_80092FBC(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092FBC(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_8009352C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_8009352C on the entry's two objects.
pub fn pc_cb_8009352C(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_8009352C(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_800935B8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_800935B8 on the entry's two objects.
pub fn pc_cb_800935B8(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_800935B8(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80092E7C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80092E7C on the entry's two objects.
pub fn pc_cb_80092E7C(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80092E7C(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_800935F8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_800935F8 on the entry's two objects.
pub fn pc_cb_800935F8(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_800935F8(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_8009361C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_8009361C on the entry's two objects.
pub fn pc_cb_8009361C(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_8009361C(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_800936F0 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_800936F0 on the entry's two objects.
pub fn pc_cb_800936F0(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_800936F0(@ptrCast(a), @ptrCast(@alignCast(b)));
}

/// orig: pc_cb_80093640 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Room-effect table entry: runs func_80093640 on the entry's two objects.
pub fn pc_cb_80093640(a: ?*v.func_80092B04_t, b: ?*v.func_80092B04_t2) callconv(.c) c_int {
    // The C's cast to the 4-aligned func_80092F74_t2 never checks alignment.
    @setRuntimeSafety(false);
    return c.func_80093640(@ptrCast(a), @ptrCast(@alignCast(b)));
}
