//! An effect-script opcode: a camera move to the saved camera (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D67C4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Keyframe 0 from the current camera (func_800D1B18), keyframe 1 = D_800F55A8, over `frames` (0 means 1) frames: func_800D1E20(2).
pub fn func_800D67C4(script: ?*v.D_800F53B8_t, frames_arg: u8) callconv(.c) void {
    _ = script;
    var frames = frames_arg;
    const keys = v.g.D_800F5520();
    c.func_800D1B18(@ptrCast(&keys[0]));
    keys[1] = v.g.D_800F55A8().*;
    v.g.D_800F54D0().* = 0;
    if (frames == 0) {
        frames = 1;
    }
    v.g.D_800F55A0().* = frames;
    c.func_800D1E20(2);
}
