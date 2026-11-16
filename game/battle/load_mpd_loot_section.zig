//! Copies the loot section of the loaded MPD room into its section 13 block
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _loadMpdLootSection (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies `data` over the loot part of the room's section 13, if that section is loaded.
pub fn loadMpdLootSection(data: ?*anyopaque) callconv(.c) void {
    if (psx.get(v.g.vs_battle_roomData().section13)) |section| {
        c.vs_main_memcpy(&section.loot, data, @as(c_uint, @sizeOf(@TypeOf(section.loot))));
    }
}
