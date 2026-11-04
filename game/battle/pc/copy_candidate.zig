//! Copying an action candidate (D2: 73AE0.s; oracle pc/src/battle_helpers.c).

/// orig: func_800DC2E0 (pc/src/battle_helpers.c)
/// Copies one 0x14-byte action candidate (five words) from `src` to `dst`.
pub fn copyCandidate(dst: [*]align(1) c_uint, src: [*]align(1) const c_uint) callconv(.c) void {
    var i: usize = 0;
    while (i < 5) : (i += 1) dst[i] = src[i]; // 5 words: the candidate size
}
