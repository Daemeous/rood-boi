//! Hides a battle model while its matrix is set up, then restores its visibility
//! (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A4828 (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Sets model slot `index` up with `matrix` while it is hidden, then restores its visibility.
pub fn func_800A4828(index: c_int, matrix: ?*v.MATRIX) callconv(.c) void {
    const temp = psx.at(v.g.vs_actors_actorRecords(), index).*.?;
    const visible = psx.getBits(&temp.unk0, "visible");
    psx.setBits(&temp.unk0, "visible", 0);
    temp.framesPerStep = 1;
    c.vs_render_prepareActorCameraMatrices(@ptrCast(matrix));
    c.vs_actors_actorRootOffset(@ptrCast(temp));
    c.vs_actors_drawActorModel(@ptrCast(temp), @ptrCast(matrix), 0);
    psx.setBits(&temp.unk0, "visible", visible);
}
