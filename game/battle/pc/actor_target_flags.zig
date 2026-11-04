//! Target-flag bits of an action (D2: 73C84.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const v = @import("views_battle_helpers");

/// orig: action_target_bit (pc/src/battle_helpers.c)
/// Bit `bit` of the action's target flags (halfword at 0xE of its 0x34-byte
/// entry). For action 0x60 against an actor whose stat block byte 0x37 has
/// class 2 in its low 3 bits the original returns the whole shifted halfword
/// xor 1, not only the inverted bit.
fn actionTargetBit(action: c_int, id: c_int, comptime bit: u4) c_int {
    const actor: [*]u8 = @ptrCast(psx.at(v.g.vs_battle_actors(), id).*.?);
    const stats: ?[*]u8 = @ptrFromInt(@as(usize, psx.byteAt(u32, actor, 0x3c).*));
    const flags: c_int = @as(c_int, psx.byteAt(u16, v.g.actions(), @as(isize, action *% 0x34) + 0xe).*) >> bit;
    if (action == 0x60 and (stats.?[0x37] & 7) == 2) return flags ^ 1;
    return flags & 1;
}

/// orig: func_800DC484 (pc/src/battle_helpers.c)
/// Bit 15 of the action's target flags (see actionTargetBit).
pub fn actionTargetBit15(action: c_int, id: c_int) callconv(.c) c_int {
    return actionTargetBit(action, id, 15);
}

/// orig: func_800DC48C (pc/src/battle_helpers.c)
/// Bit 14 of the action's target flags (see actionTargetBit).
pub fn actionTargetBit14(action: c_int, id: c_int) callconv(.c) c_int {
    return actionTargetBit(action, id, 14);
}
