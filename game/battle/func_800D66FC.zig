//! An effect-script opcode: a camera move to look at an entry (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D66FC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Both keyframes from the current camera, keyframe 1 looking at unk1C[index]'s translation, over `frames` (0 means 1): func_800D1E20(2).
pub fn func_800D66FC(script: ?*v.D_800F53B8_t, index: u8, frames_arg: u8) callconv(.c) void {
    const s = script.?;
    var frames = frames_arg;
    const keys = v.g.D_800F5520();
    c.func_800D1B18(@ptrCast(&keys[0]));
    c.func_800D1B18(@ptrCast(&keys[1]));
    const entry = psx.at(&s.unk1C, index); // unk1C[16]: the index is not checked
    keys[1].lookAt.vx = entry.unk38.t[0] *% 4096;
    keys[1].lookAt.vy = entry.unk38.t[1] *% 4096;
    keys[1].lookAt.vz = entry.unk38.t[2] *% 4096;
    v.g.D_800F54D0().* = 0;
    if (frames == 0) {
        frames = 1;
    }
    v.g.D_800F55A0().* = frames;
    c.func_800D1E20(2);
}
