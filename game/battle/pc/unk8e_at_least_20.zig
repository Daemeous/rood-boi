//! The action context's halfword at 0x8E (D2: 6FA6C.s; oracle pc/src/battle_helpers.c).

/// orig: func_800D826C (pc/src/battle_helpers.c)
/// Whether the unsigned halfword at 0x8E of the action context is at least 0x20.
pub fn unk8eAtLeast20(context: [*]const u8) callconv(.c) c_int {
    const value = @as(*align(1) const u16, @ptrCast(context + 0x8e)).*;
    return @intFromBool(value >= 0x20);
}
