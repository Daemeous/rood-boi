//! Starts a P-file block 2 sequence (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_restartTextureAnimation (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// unk16 = table; unk18 = block 2 + its (index + 2)th u16 offset (bytes); unkE and unk6 cleared.
pub fn vs_effects_restartTextureAnimation(sequence: [*c]v.func_800D6CF_t, table: c_int, index: c_int) callconv(.c) void {
    const s: *v.func_800D6CF_t = sequence;
    s.unk16 = psx.conv(u8, table);
    const block2: [*]u16 = @ptrCast(psx.get(v.g.vs_effects_package().*.?.block2Data).?);
    const offset: usize = psx.at(block2, index +% 2).*;
    psx.put(&s.unk18, @as(*u8, @ptrFromInt(@intFromPtr(block2) +% offset)));
    s.unkE = 0;
    s.unk6 = 0;
}
