//! Flips a room geometry entry's sign and swaps its 0x18/0x1C words (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: vs_field_finishRoomTiltRotation (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Mirrors room geometry entry `arg0`: writes its 0x18 word into the room geometry and negates and swaps its own fields.
pub fn vs_field_finishRoomTiltRotation(arg0: c_int) callconv(.c) void {
    const entry = psx.at(v.g.vs_field_roomEffectInstances(), arg0);
    const temp_a2 = psx.get(entry.unk4).?;
    // temp_a0 is a void* in the C: unk0 is a byte offset, and [4] and [6] are int-sized steps from it.
    const base = psx.get(v.g.vs_battle_roomData().geometrySection).?;
    const off: isize = @as(isize, temp_a2.geometryOffset);

    if (temp_a2.axis == 2) {
        psx.byteAt(c_int, base, off + 16).* = temp_a2.restAngle;
    } else {
        psx.byteAt(c_int, base, off + 24).* = temp_a2.restAngle;
    }
    const temp_v0: c_int = -%temp_a2.initialVelocity;
    const temp_a1: c_int = temp_a2.startAngle;
    temp_a2.initialVelocity = temp_v0;
    temp_a2.angularVelocity = temp_v0;
    temp_a2.startAngle = temp_a2.restAngle;
    temp_a2.restAngle = temp_a1;
    temp_a2.acceleration = -%temp_a2.acceleration;
}
