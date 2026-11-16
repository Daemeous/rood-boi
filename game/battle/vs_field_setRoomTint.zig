//! Stores the flag record D_800F1BB0 from a 4-byte source (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_setRoomTint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the three flag bytes of `arg0` and `arg1` into D_800F1BB0; clears vs_field_isRoomTintActive when all are zero.
pub fn vs_field_setRoomTint(arg0: ?*v.D_800F4538_unk58_2, arg1: c_int) callconv(.c) void {
    const src = arg0.?;
    if (src.unk0 == 0 and src.unk1 == 0 and src.unk2 == 0 and arg1 == 0) {
        v.g.vs_field_isRoomTintActive().* = 0;
        return;
    }
    const rec = v.g.D_800F1BB0();
    rec.unk6 = 1;
    rec.unk4 = psx.conv(i16, arg1);
    rec.unk0 = psx.conv(i8, src.unk0);
    rec.unk1 = psx.conv(i8, src.unk1);
    rec.unk2 = psx.conv(i8, src.unk2);
}
