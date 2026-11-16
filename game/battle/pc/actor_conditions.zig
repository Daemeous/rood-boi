//! Condition bytes of an actor and its action slots (D2: 7C4F4.s; oracle pc/src/battle_helpers.c).
const std = @import("std");
const ai = @import("views_71618");
const psx = @import("common").psx;
const v = @import("views_battle_helpers");

/// The stats record of an actor state: its actor's stats pointer.
fn statsOf(state: *ai.vs_battle_aiState) *ai.vs_battle_actor2 {
    return psx.get(psx.get(state.actor).?.stats).?;
}

/// The paired 16-bit stats at 0x18 (current HP, max HP) and 0x1C (current MP, max MP) as the original's
/// words: the low half the current value, the high half the maximum, both unsigned.
fn pair(current: i16, max: i16) c_uint {
    return @as(c_uint, @as(u16, @bitCast(current))) | (@as(c_uint, @as(u16, @bitCast(max))) << 16);
}

comptime {
    // The offsets the original used: limb row * 0xDC from 0x398, armor slot * 4 from 0x8C0.
    std.debug.assert(@sizeOf(ai.vs_battle_uiEquipment_limb) == 0xdc and @offsetOf(ai.vs_battle_actor2, "limbs") == 0x398);
    std.debug.assert(@sizeOf(ai.vs_battle_actor_unk8C0) == 4 and @offsetOf(ai.vs_battle_actor2, "armor") == 0x8c0);
    std.debug.assert(@offsetOf(ai.vs_battle_actor2, "weapon") + @offsetOf(ai.vs_battle_uiWeapon, "currentPp") == 0x14c);
}

/// orig: vs_battle_aiRefreshAiActionUsability (pc/src/battle_helpers.c)
/// Decides whether action slot (row, column) is usable and records it in bit 7
/// of the slot's flags (word at 0x230 + slot * 8). Type 1 checks a signed
/// 16-bit resource and a blocking flag; type 3 compares cost plus half the
/// high stat against the low stat; type 4 checks a second signed resource.
/// A 0x2000 state allows only types 1 and 2. Rows with a count below 2 are
/// never usable.
pub fn slotUsable(state: *ai.vs_battle_aiState, row: c_int, column: c_int) callconv(.c) c_int {
    const stats = statsOf(state);
    const slot: c_int = row *% 4 +% column;
    var usable: c_int = 0;
    // armor[row][column] is slot * 4 bytes on from armor[0][0]; a row or column outside the table reads on, as in C.
    const action: c_uint = @as(u16, @bitCast(psx.at(&stats.armor[0][0], slot).id));
    const flags: c_uint = @bitCast(stats.statuses);
    const hp = pair(stats.currentHP, stats.maxHP);
    if (psx.at(&stats.limbs, row).hp >= 2) {
        const entry = psx.byteAt(u8, v.g.actions(), @as(isize, @intCast(action * 0x34))); // action <= 0xFFFF
        const kind: c_int = (psx.byteAt(u8, entry, 2).* >> 1) & 7;
        const cost: c_int = psx.byteAt(u8, entry, 3).*;
        if (flags & 0x2000 == 0 or (@as(c_int, 1) << @intCast(kind)) & 6 != 0) {
            if (kind == 1) {
                usable = @intFromBool(!(stats.currentMP < cost) and flags & 0x1000 == 0);
            } else if (kind == 3) {
                usable = @intFromBool(!(@as(c_int, @intCast(hp & 0xffff)) < cost +% @as(c_int, @intCast((hp >> 16) >> 1))));
            } else if (kind == 4) {
                usable = @intFromBool(!(@as(i16, @bitCast(stats.weapon.currentPp)) < cost));
            } else {
                usable = 1;
            }
        }
    }
    const slot_flags = psx.fieldAt(c_uint, &state.configs, @as(isize, slot *% 8));
    slot_flags.* = (slot_flags.* & ~@as(c_uint, 0x80)) | (@as(c_uint, @bitCast(usable)) << 7);
    return usable;
}

/// orig: vs_battle_aiRefreshAiResourceRestrictions (pc/src/battle_helpers.c)
/// Derives three condition bytes from the actor's paired 16-bit stats (low =
/// current, high = maximum) and the state threshold at 0x17: 0x12 = low1 * 2
/// < high1; 0x11 = threshold and (low2 < threshold or low2 * 2 < high2);
/// 0x10 = status flags. The original loads 0x90000 with lui and then
/// overwrites it with ori $zero, so the status mask is 0xE2B0.
pub fn setConditionBytes(state: *ai.vs_battle_aiState) callconv(.c) void {
    const stats = statsOf(state);
    const first = pair(stats.currentHP, stats.maxHP);
    const second = pair(stats.currentMP, stats.maxMP);
    const flags: c_uint = @bitCast(stats.statuses);
    const threshold: c_int = state.maxActionCost;
    const low: c_int = @intCast(second & 0xffff);
    const high: c_int = @intCast(second >> 16);
    state.unk12 = @intFromBool(@as(c_int, @bitCast((first & 0xffff) *% 2)) < @as(c_int, @intCast(first >> 16)));
    state.unk11 = @intFromBool(threshold != 0 and (low < threshold or low *% 2 < high));
    state.unk10 = @intFromBool(flags & 0xe2b0 != 0 or (threshold != 0 and flags & 0x1000 != 0));
}
