//! Copies a four-word vector message into the function's state, then runs vs_field_writeRoomEffectTransform (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_setRoomEffectTransform (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Copies three words and a short vector from `src` into `dst`, then calls vs_field_writeRoomEffectTransform on `dst`.
pub fn vs_field_setRoomEffectTransform(arg0: ?*v.func_80092F74_t, arg1: ?*v.vs_field_RoomEffectCommand) callconv(.c) c_int {
    const dst = arg0.?;
    const src = arg1.?;
    dst.unkC = src.operands[0].s32;
    dst.unk10 = src.operands[1].s32;
    dst.unk14 = src.operands[2].s32;
    dst.unk1C.vx = psx.conv(i16, src.operands[3].s32);
    dst.unk1C.vy = psx.conv(i16, src.unk14);
    dst.unk1C.vz = psx.conv(i16, src.unk18);
    c.vs_field_writeRoomEffectTransform(psx.cptr(c.func_80092F74_t, dst));
    return 0;
}
