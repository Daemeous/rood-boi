//! Hit-entity reset (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_initializeActionHit (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Zeroes a hit entity, sets its unk0.unk3 to 6, and clears the word after inflictStatuses.
pub fn vs_battle_initializeActionHit(hit: ?*v._hitEntity_t) callconv(.c) void {
    c.vs_main_bzero(hit, @sizeOf(v._hitEntity_t));
    hit.?.unk0.unk3 = 6;
    // *(&arg0->inflictStatuses + 1): the element after inflictStatuses, an int.
    psx.at(&hit.?.inflictStatuses, 1).* = 0;
}
