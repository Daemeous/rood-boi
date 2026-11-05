//! A battle helper that returns at once (D2: 7E05C.s; oracle pc/src/battle_helpers.c).

/// orig: func_800E6EAC (pc/src/battle_helpers.c)
/// Does nothing; its PS1 delay slot only wrote a caller-saved register.
pub fn idleHook() callconv(.c) void {}
