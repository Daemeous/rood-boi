//! Fills the slots of vs_effects_cameraShakeChannels selected by the bits of arg0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_startCameraShakeChannels (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets up each slot whose bit is in `mask`, takes its status from vs_effects_randomCameraShakePhaseStep and marks it in the flags.
pub fn vs_effects_startCameraShakeChannels(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int, arg4: c_int, arg5: c_int, arg6: c_int) callconv(.c) void {
    for (0..4) |i| {
        const bit: u5 = @intCast(i);
        if (((arg0 >> bit) & 1) != 0) {
            const p: *v.D_800F54B8_t = &v.g.vs_effects_cameraShakeChannels()[i];
            psx.setBits(p, "unk1_0", psx.conv(u5, arg1));
            p.unk4 = psx.conv(u8, arg2);
            psx.setBits(p, "unk1_5", psx.conv(u1, arg3));
            p.unk2 = psx.conv(u8, arg4);
            psx.setBits(p, "unk1_6", psx.conv(u1, arg5));
            p.unk3 = psx.conv(u8, arg6);
            p.unk0 = 0;
            p.unk5 = c.vs_effects_randomCameraShakePhaseStep(@ptrCast(p));
            c.vs_effects_enableCameraShakeChannels(@as(c_int, 1) << bit);
        }
    }
}
