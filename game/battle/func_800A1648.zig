//! Armature data of a battle actor's model: one armature's part, or -1/-3 when absent (src/BATTLE/BATTLE.PRG/38C1C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");

/// orig: func_800A1648 (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Reads one armature field of an actor's model: part 0 is unk6, parts 1 and 2 the high and low nibble of unk7; -1 with no actor, -3 for an empty nibble.
pub fn func_800A1648(actorIndex: c_int, armatureIndex: c_int, part: c_int) callconv(.c) c_int {
    var actor: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), actorIndex).*;
    if (actor == null) {
        const alt: ?*v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), actorIndex).*;
        if (alt == null) return -1;
        actor = @ptrCast(alt);
    }
    const model = psx.get(actor.?.unk0.unk68);
    // No default in the C: any other part returns the uninitialised v (the
    // object returns whatever is in its result register); every caller passes 0.
    var result: c_uint = undefined;
    switch (part) {
        0 => result = psx.at(&model.?.armatures, armatureIndex).unk6,
        1 => {
            result = psx.at(&model.?.armatures, armatureIndex).unk7 >> 4;
            if (result == 0) return -3;
        },
        2 => {
            result = psx.at(&model.?.armatures, armatureIndex).unk7 & 0xF;
            if (result == 0) return -3;
        },
        else => {},
    }
    return @bitCast(result);
}
