//! Derives a battle object's world position from its rotated offset (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_placeParticleSpawn (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Rotates the object's offset into its local vector when flagged, then writes its world position from the offset and the base position.
pub fn vs_effects_placeParticleSpawn(object: ?*v.func_800FA098_arg1, out: ?*v.func_800D2904_t, rotation: *const gte.View) callconv(.c) void {
    const a = object.?;
    if ((a.flags & 0x40000) != 0) {
        gte.rotateTo(&a.unk34, rotation, &a.unk2C);
    } else {
        a.unk34.vx = a.unk2C.vx;
        a.unk34.vy = a.unk2C.vy;
        a.unk34.vz = a.unk2C.vz;
    }
    const o = out.?;
    o.unkC = a.unk98.vx +% a.unk34.vx *% 4096;
    o.unk10 = a.unk98.vy +% a.unk34.vy *% 4096;
    o.unk14 = a.unk98.vz +% a.unk34.vz *% 4096;
}
