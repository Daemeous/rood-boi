//! Snaps the height of each grounded actor in slots 2..16 to the ground under it,
//! lowered by any raised object on its tile (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: vs_field_snapRoomActorsToSupport (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Sets each grounded actor's height from the ground under it and the objects on its tile.
pub fn vs_field_snapRoomActorsToSupport() callconv(.c) void {
    const actors = v.g.vs_actors_actorRecords();
    const objs = v.g.vs_field_gimmickInstances();

    var i: c_int = 2;
    while (i < 17) : (i +%= 1) {
        const actor = actors[@intCast(i)] orelse continue;

        if (psx.getBits(&actor.unk0, "unkA_5") == 0 and
            actor.unk0.unk5D < 2 and
            psx.getBits(&actor.unk0, "unkA_0") == 0)
        {
            const x: c_int = actor.unk0.position.vx;
            const z: c_int = actor.unk0.position.vz;
            var h: c_int = c.vs_field_interpolatedFloorHeight(x, z);
            // Sign-extend the low 15 bits: h <<= 17; h >>= 17.
            h = (h << 17) >> 17;

            var j: c_int = 0;
            while (j < 16) : (j +%= 1) {
                const obj = objs[@intCast(j)] orelse continue;
                if (@as(c_int, obj.unk1C) == x and
                    @as(c_int, obj.unk20) == z and
                    @as(c_int, obj.unk1E) -% 0x80 < h)
                {
                    h = @as(c_int, obj.unk1E) -% 0x80;
                }
            }

            actor.unk0.position.vy = psx.conv(i16, h);
        }
    }
}
