//! Forwarding a value to an actor (D2: 6FA0C.s; oracle pc/src/battle_helpers.c).
const c = @import("c_battle_helpers");

/// orig: func_800D820C (pc/src/battle_helpers.c)
/// Forwards (id, value) to func_80095B7C for a nonzero actor id and returns 0;
/// id 0 does nothing and returns 1 (the assembly falls into func_800D826C's
/// tail there; callers ignore the result).
pub fn sendActorValue(id: c_int, value: c_int) callconv(.c) c_int {
    if (id == 0) return 1;
    c.func_80095B7C(id, value);
    return 0;
}
