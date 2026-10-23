//! Sound-effect volume and pan of a battle actor (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AD62C (src/BATTLE/BATTLE.PRG/40564.c)
/// Writes the sound-effect volume and pan of an actor, refreshing its position first when it is moving or unplaced.
pub fn func_800AD62C(actor: c_int, volume: [*c]c_int, pan: [*c]c_int) callconv(.c) void {
    const s = psx.at(v.g.D_800F4538(), actor).*.?;

    if (s.unk17E4.unk0 == 2) {
        volume[0] = 0x80;
        if (pan != null) {
            pan[0] = 0;
        }
        return;
    }

    const flags = &s.unk0;
    if (psx.getBits(flags, "unk9_0") != 0 or
        psx.getBits(flags, "unk9_4") != 0 or
        psx.getBits(flags, "unk9_7") == 0 or
        psx.getBits(flags, "unkA_0") != 0 or
        psx.getBits(flags, "unkA_3") != 0 or
        psx.getBits(flags, "unkA_5") != 0)
    {
        c.func_800A1280(actor, 0xFF, psx.cptr(c.SVECTOR, &s.unk6FC), 0);
    }

    // *(int*)&unk6FC: the first four bytes of the SVECTOR, read unaligned.
    const sfx_xy: *align(1) c_int = @ptrCast(&s.unk6FC);
    const temp_v0: c_int = c.vs_main_computeSfxPan(sfx_xy.*, @as(c_int, s.unk6FC.vz));

    volume[0] = temp_v0 >> 0x10;
    if (s.unk17E4.unk1 != 0) {
        volume[0] = @as(c_int, s.unk17E4.unk1);
    }

    if (pan != null) {
        pan[0] = temp_v0 & 0xFFFF;
        if (s.unk17E4.unk2 != 0) {
            pan[0] = @as(c_int, s.unk17E4.unk2);
        }
    }
}
