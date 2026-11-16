//! Dispatches a battle record by its code (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_resolveTransformAttachment (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Codes below 0x10 fill the record from two helpers; 0x10 and 0x11 run vs_effects_buildCameraAttachment, 0x12 runs vs_effects_buildEffectOriginAttachment.
pub fn vs_effects_resolveTransformAttachment(arg0: c_int, arg1: ?*v.func_800D0C60_t) callconv(.c) void {
    const rec = arg1.?;
    if (rec.unk34 < 0x10) {
        var sp20: [2]c_int = undefined;
        c.func_800A1720(@as(c_int, rec.unk34), @as(c_int, rec.unk35), psx.cptr(c.func_800CFE98_t, &rec.unk78), &sp20);
        var sp10: v.func_8006EBF8_t = undefined;
        _ = c.vs_actors_actorPlacement(@as(c_int, rec.unk34), &sp10);
        rec.unkBE = sp10.unk0.unk4.pad;
        return;
    }
    switch (rec.unk34) {
        0x10, 0x11 => c.vs_effects_buildCameraAttachment(arg0, psx.cptr(c.func_800D0C60_t, rec), @as(c_int, @intFromBool(rec.unk34 != 0x10))),
        0x12 => c.vs_effects_buildEffectOriginAttachment(psx.cptr(c.func_800CFE98_t, &rec.unk78)),
        else => {},
    }
}
