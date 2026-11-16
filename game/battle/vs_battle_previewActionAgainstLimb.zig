//! Builds a single-target action request and prices it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_previewActionAgainstLimb (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills an action request aimed at one target and returns the action's cost byte.
pub fn vs_battle_previewActionAgainstLimb(action: c_int, actorIndex: c_int, targetActor: c_int, targetLimb: c_int, arg4: ?*anyopaque) callconv(.c) c_int {
    // ROOD_PC: the whole request is zeroed first, padding included.
    var sp10: v.D_800F19CC_t2 = std.mem.zeroes(v.D_800F19CC_t2);

    sp10.unk4C[0].unk0.targetActor = psx.conv(u8, targetActor);
    sp10.actionIndex = psx.conv(u16, action);
    sp10.unk4.unk40 = 0;
    sp10.unk4.unk0.targetActor = psx.conv(u8, actorIndex);
    sp10.unk4A = 1;
    sp10.unk4C[0].unk40 = 0;
    sp10.unk4C[0].unk0.targetLimb = psx.conv(i8, targetLimb);

    c.vs_battle_resolveActionTargets(action, @ptrCast(arg4), @ptrCast(&sp10), 0);

    const actor = psx.at(v.g.vs_battle_actors(), actorIndex).*.?;
    const stats = psx.get(actor.stats);
    const cost: c_uint = @bitCast(c._getActionCost(psx.conv(c_uint, action), @ptrCast(stats), 0));
    return @bitCast(cost & 0xFF00_0000);
}
