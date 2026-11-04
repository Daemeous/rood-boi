//! Condition bytes of an actor and its action slots (D2: 7C4F4.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const v = @import("views_battle_helpers");

/// The stats record of an actor state: the guest pointer at 0x3C of the record the state points to at 0x54.
fn statsOf(state: [*]u8) [*]u8 {
    const record: [*]u8 = @ptrFromInt(@as(usize, psx.byteAt(u32, state, 0x54).*));
    return @ptrFromInt(@as(usize, psx.byteAt(u32, record, 0x3c).*));
}

/// orig: func_800E4DF8 (pc/src/battle_helpers.c)
/// Decides whether action slot (row, column) is usable and records it in bit 7
/// of the slot's flags (word at 0x230 + slot * 8). Type 1 checks a signed
/// 16-bit resource and a blocking flag; type 3 compares cost plus half the
/// high stat against the low stat; type 4 checks a second signed resource.
/// A 0x2000 state allows only types 1 and 2. Rows with a count below 2 are
/// never usable.
pub fn slotUsable(state: [*]u8, row: c_int, column: c_int) callconv(.c) c_int {
    const stats = statsOf(state);
    const slot: c_int = row *% 4 +% column;
    var usable: c_int = 0;
    const action: c_uint = psx.byteAt(u16, stats, 0x8c0 + @as(isize, slot *% 4)).*;
    const flags = psx.byteAt(c_uint, stats, 0x948).*;
    const pair = psx.byteAt(c_uint, stats, 0x18).*;
    if (psx.byteAt(i16, stats, @as(isize, row *% 0xdc) + 0x398).* >= 2) {
        const entry = psx.byteAt(u8, v.g.actions(), @as(isize, @intCast(action * 0x34))); // action <= 0xFFFF
        const kind: c_int = (psx.byteAt(u8, entry, 2).* >> 1) & 7;
        const cost: c_int = psx.byteAt(u8, entry, 3).*;
        if (flags & 0x2000 == 0 or (@as(c_int, 1) << @intCast(kind)) & 6 != 0) {
            if (kind == 1) {
                usable = @intFromBool(!(psx.byteAt(i16, stats, 0x1c).* < cost) and flags & 0x1000 == 0);
            } else if (kind == 3) {
                usable = @intFromBool(!(@as(c_int, @intCast(pair & 0xffff)) < cost +% @as(c_int, @intCast((pair >> 16) >> 1))));
            } else if (kind == 4) {
                usable = @intFromBool(!(psx.byteAt(i16, stats, 0x14c).* < cost));
            } else {
                usable = 1;
            }
        }
    }
    const slot_flags = psx.byteAt(c_uint, state, 0x230 + @as(isize, slot *% 8));
    slot_flags.* = (slot_flags.* & ~@as(c_uint, 0x80)) | (@as(c_uint, @bitCast(usable)) << 7);
    return usable;
}

/// orig: func_800E4F14 (pc/src/battle_helpers.c)
/// Derives three condition bytes from the actor's paired 16-bit stats (low =
/// current, high = maximum) and the state threshold at 0x17: 0x12 = low1 * 2
/// < high1; 0x11 = threshold and (low2 < threshold or low2 * 2 < high2);
/// 0x10 = status flags. The original loads 0x90000 with lui and then
/// overwrites it with ori $zero, so the status mask is 0xE2B0.
pub fn setConditionBytes(state: [*]u8) callconv(.c) void {
    const stats = statsOf(state);
    const first = psx.byteAt(c_uint, stats, 0x18).*;
    const second = psx.byteAt(c_uint, stats, 0x1c).*;
    const flags = psx.byteAt(c_uint, stats, 0x948).*;
    const threshold: c_int = state[0x17];
    const low: c_int = @intCast(second & 0xffff);
    const high: c_int = @intCast(second >> 16);
    state[0x12] = @intFromBool(@as(c_int, @bitCast((first & 0xffff) *% 2)) < @as(c_int, @intCast(first >> 16)));
    state[0x11] = @intFromBool(threshold != 0 and (low < threshold or low *% 2 < high));
    state[0x10] = @intFromBool(flags & 0xe2b0 != 0 or (threshold != 0 and flags & 0x1000 != 0));
}
