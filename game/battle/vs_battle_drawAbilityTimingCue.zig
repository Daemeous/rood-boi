//! Queues a textured quad at the vs_battle_abilityTimingCuePosition position into the ordering table
//! named by the scratchpad (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const sprite_item = common.sprite_item;

fn xy(x: c_int, y: c_int) c_int {
    return @bitCast((@as(c_uint, @bitCast(x)) & 0xFFFF) | (@as(c_uint, @bitCast(y)) << 16));
}

/// orig: vs_battle_drawAbilityTimingCue (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Adds the quad (a draw item: 22 by 32 around the vs_battle_abilityTimingCuePosition position's bottom, page 23, CLUT (960, 221), colour F0 40 20) five slots
/// before the table's scene base.
pub fn vs_battle_drawAbilityTimingCue() callconv(.c) void {
    const scratch = common.scratch.get();
    const position = v.g.vs_battle_abilityTimingCuePosition();
    const x: c_int = position.vx;
    const y: c_int = position.vy;
    const corners = [4]c_int{ xy(x -% 11, y -% 32), xy(x +% 11, y -% 32), xy(x -% 11, y), xy(x +% 11, y) };
    const texels = [4]c_int{ 0xC0, 0xD6, 0xC0 | (0x20 << 8), 0xD6 | (0x20 << 8) };
    // setTPage(prim, 0, 0, 448, 256): texture page 23; setClut(prim, 960, 221).
    const tpage: c_int = ((0 & 0x3) << 7) | ((0 & 0x3) << 5) | ((256 & 0x100) >> 4) | ((448 & 0x3ff) >> 6) | ((256 & 0x200) << 2);
    const clut: c_int = (221 << 6) | ((960 >> 4) & 0x3f);
    // AddPrim(ot - 5, ...): the ordering table's entry 5 u_long (4 bytes each) back.
    const ot: ?*anyopaque = @ptrFromInt(@intFromPtr(scratch.unk4) -% 20);
    _ = sprite_item.ft4(ot, false, false, tpage, clut, 0xF0 | (0x40 << 8) | (0x20 << 16), &corners, &texels);
}
