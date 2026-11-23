//! Derives a 90-degree facing sector from a packed direction word and
//! nudges three axis bytes of the output (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_tileInFrontOfActor (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Writes three direction bytes from the packed word, then steps one of them by the sector of the rotation.
pub fn vs_field_tileInFrontOfActor(info: ?*v.func_8006EBF8_t, offsets: [*c]u8) callconv(.c) c_int {
    const a = info.?;
    var var_v1: c_int = (@as(c_int, a.location.position.pad) +% 0x200) & 0xFFF;
    if (var_v1 < 0) {
        var_v1 +%= 0x3FF;
    }
    offsets[0] = psx.getBits(&a.location.packedTile.fields, "unk0_0");
    offsets[2] = psx.getBits(&a.location.packedTile.fields, "unk0_16");
    offsets[1] = psx.getBits(&a.location.packedTile.fields, "unk0_8");

    var_v1 = common.analog.cardinal(var_v1); // PC: Ashley's side with hysteresis under analog movement (analog_move.c); else var_v1 >> 10
    switch (var_v1) {
        0 => offsets[2] -%= 1,
        1 => offsets[0] -%= 1,
        2 => offsets[2] +%= 1,
        else => offsets[0] +%= 1,
    }
    return var_v1;
}
