//! Flips a room geometry entry's sign and swaps its 0x18/0x1C words (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80093824 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Mirrors room geometry entry `arg0`: writes its 0x18 word into the room geometry and negates and swaps its own fields.
pub fn func_80093824(arg0: c_int) callconv(.c) void {
    const entry = psx.at(v.g.D_800F1DD8(), arg0);
    const temp_a2 = psx.get(entry.unk4).?;
    // temp_a0 is a void* in the C: unk0 is a byte offset, and [4] and [6] are int-sized steps from it.
    const base = psx.get(v.g.vs_battle_roomData().geometrySection).?;
    const off: isize = @as(isize, temp_a2.unk0);

    if (temp_a2.unk7 == 2) {
        psx.byteAt(c_int, base, off + 16).* = temp_a2.unk18;
    } else {
        psx.byteAt(c_int, base, off + 24).* = temp_a2.unk18;
    }
    const temp_v0: c_int = -%temp_a2.unk20;
    const temp_a1: c_int = temp_a2.unk1C;
    temp_a2.unk20 = temp_v0;
    temp_a2.unk8 = temp_v0;
    temp_a2.unk1C = temp_a2.unk18;
    temp_a2.unk18 = temp_a1;
    temp_a2.unkC = -%temp_a2.unkC;
}
