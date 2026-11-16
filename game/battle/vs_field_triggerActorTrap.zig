//! Points the battle's pending effect request at a target actor and queues the effect (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_triggerActorTrap (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills the battle request for the target actor's limb and queues effect `arg0.unk6 + 6` from `arg1`'s position.
pub fn vs_field_triggerActorTrap(arg0: ?*v._mpdRoomSectionA, arg1: ?*v.func_8006EBF8_t, targetActor: c_int) callconv(.c) void {
    var sp10: v.func_800CF0E8_t = std.mem.zeroes(v.func_800CF0E8_t);

    v.g.vs_battle_actionState().*.?.unk8.primaryTarget.unk40 = 3;
    v.g.vs_battle_actionState().*.?.unk8.targetCount = 1;
    v.g.vs_battle_actionState().*.?.unk8.targets[0].unk40 = 0;
    v.g.vs_battle_actionState().*.?.unk8.targets[0].target.targetActor = psx.conv(u8, targetActor);
    const actor = psx.get(psx.at(v.g.vs_battle_actors(), targetActor).*.?.stats).?;
    v.g.vs_battle_actionState().*.?.unk8.targets[0].target.targetLimb = psx.conv(i8, psx.getBits(actor, "unk36"));
    v.g.vs_battle_actionState().*.?.unk4 = 1;
    v.g.vs_battle_actionState().*.?.unk0 = 0;
    v.g.vs_battle_actionState().*.?.rangeOrigin.vx = arg1.?.location.position.vx;
    v.g.vs_battle_actionState().*.?.rangeOrigin.vy = arg1.?.location.position.vy;
    v.g.vs_battle_actionState().*.?.rangeOrigin.vz = arg1.?.location.position.vz;
    c.vs_battle_resolveActionTargets(
        @as(c_int, arg0.?.trigger.unk6) +% 6,
        @ptrCast(&v.g.vs_battle_actionState().*.?.queue[0]),
        @ptrCast(&v.g.vs_battle_actionState().*.?.unk8),
        1,
    );
    sp10.effectId = psx.conv(u16, @as(c_int, arg0.?.trigger.unk6) +% 6);
    sp10.unk4 = 5;
    sp10.unk10 = 4;
    sp10.unk2 = 1;
    sp10.unk3 = 0;
    sp10.unk14.u8[0] = psx.conv(u8, targetActor);
    sp10.unk16.u8[0] = 0xFD;
    _ = c.vs_effects_beginSecondaryEffects(psx.cptr(c.func_800CF0E8_t, &sp10), 0);
}
