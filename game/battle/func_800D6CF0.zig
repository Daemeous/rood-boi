//! Starts a P-file block 2 sequence (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800D6CF0 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// unk16 = table; unk18 = block 2 + its (index + 2)th u16 offset (bytes); unkE and unk6 cleared.
pub fn func_800D6CF0(sequence: [*c]v.func_800D6CF_t, table: c_int, index: c_int) callconv(.c) void {
    const s: *v.func_800D6CF_t = sequence;
    s.unk16 = psx.conv(u8, table);
    const block2: [*]u16 = @ptrCast(psx.get(v.g.D_800F569C().*.?.block2Data).?);
    const offset: usize = psx.at(block2, index +% 2).*;
    psx.put(&s.unk18, @as(*u8, @ptrFromInt(@intFromPtr(block2) +% offset)));
    s.unkE = 0;
    s.unk6 = 0;
}
