//! Scales the scratchpad actor transform offset by the actor's size and rotation (pc/src/game/BATTLE/BATTLE.PRG/44F14.c).
const psx = @import("common").psx;
const pad = @import("common").pad;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: vs_actors_actorRootOffset (pc/src/game/BATTLE/BATTLE.PRG/44F14.c)
/// Sets the scale defaults, derives the scratchpad offset from the actor's extents and scale, and copies its rotation.
pub fn vs_actors_actorRootOffset(actor: ?*v.D_800F4538_t) callconv(.c) void {
    const a = actor.?;
    const scratch: *v.actorTransformScratch = pad.xform(v.actorTransformScratch);

    v.g.D_800F2458().scales[0] = 64;
    v.g.D_800F2458().scales[1] = 64;
    v.g.D_800F2458().scales[2] = 64;

    // 0x10001000 as one int covers unk2C and unk2E, both 0x1000 (little endian).
    if (psx.fieldAt(c_int, &a.unk0.scaleX, 0).* == 0x10001000 and a.unk0.scaleZ == 4096) {
        scratch.offset.vx = psx.conv(i16, @as(c_int, psx.conv(i16, a.blendPose.unk0[41].vx)) >> 1);
        scratch.offset.vy = psx.conv(i16, @as(c_int, psx.conv(i16, a.blendPose.unk0[41].vy)) >> 1);
        scratch.offset.vz = psx.conv(i16, @as(c_int, psx.conv(i16, a.blendPose.unk0[41].vz)) >> 1);
    } else {
        scratch.offset.vx = psx.conv(i16, @divTrunc(@as(c_int, a.blendPose.unk0[41].vx) *% @as(c_int, a.unk0.scaleX), 8192));
        scratch.offset.vy = psx.conv(i16, @divTrunc(@as(c_int, a.blendPose.unk0[41].vy) *% @as(c_int, a.unk0.scaleY), 8192));
        scratch.offset.vz = psx.conv(i16, @divTrunc(@as(c_int, a.blendPose.unk0[41].vz) *% @as(c_int, a.unk0.scaleZ), 8192));
    }

    scratch.offset.vx = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vx) *% @as(c_int, a.unk183C), 4096));
    scratch.offset.vy = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vy) *% @as(c_int, a.unk183C), 4096));
    scratch.offset.vz = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vz) *% @as(c_int, a.unk183C), 4096));

    c.vs_actors_transformRootOffset(@ptrCast(a));

    scratch.angleX = a.unk0.unk14;
    scratch.angleY = a.unk0.unk16;
}
