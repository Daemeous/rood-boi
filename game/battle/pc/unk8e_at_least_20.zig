//! The action context's halfword at 0x8E (D2: 6FA6C.s; oracle pc/src/battle_helpers.c).

const ai = @import("views_71618");
/// orig: vs_battle_aiIsEngagementReady (pc/src/battle_helpers.c)
/// Whether the unsigned halfword at 0x8E of the action context is at least 0x20.
pub fn unk8eAtLeast20(context: *const ai.vs_battle_aiState) callconv(.c) c_int {
    const value = context.timer8E;
    return @intFromBool(value >= 0x20);
}
