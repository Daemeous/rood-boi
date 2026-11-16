//! Requests an effect on the battle actors other than one slot (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007BE20 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sends effect `effectId` to the live actors except `actorSlot`, listing their slots in the request.
pub fn func_8007BE20(arg0: c_int, arg1: c_int) callconv(.c) void {
    const effectId = arg0;
    const actorSlot = arg1;

    var sp10: v.func_800CF0E8_t = std.mem.zeroes(v.func_800CF0E8_t);
    const actors = v.g.vs_battle_actors();
    var var_a2: c_int = 0;

    sp10.unk2 = 1;
    sp10.unk4 = 5;
    sp10.effectId = psx.conv(u16, effectId);
    sp10.unk3 = 0;
    sp10.unkC = 0;
    sp10.unkA.s16 = 0;
    sp10.unk8.s16 = 0;
    sp10.unk10 = 4;
    sp10.unk14.u8[0] = psx.conv(u8, actorSlot);
    sp10.unk16.u8[0] = 0;

    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        if (actors[@intCast(i)] != null and i != actorSlot) {
            // var_a2 counts at most the 16 slots, so it indexes unkD0 in range.
            sp10.unkD0[@intCast(var_a2)] = psx.conv(u8, i);
            var_a2 +%= 1;
        }
    }

    sp10.unk3 = psx.conv(u8, var_a2);
    _ = c.vs_effects_beginActionEffect(psx.cptr(c.func_800CF0E8_t, &sp10), 0, 0);
}
