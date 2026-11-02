//! An effect view facing the camera (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D0984 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The effect's matrix looking from the camera's look-at to its position, placed at the look-at, or (`push`) 0x600 short of the camera along that view.
pub fn func_800D0984(arg0: c_int, effect: ?*v.func_800D0C60_t, push: c_int) callconv(.c) void {
    _ = arg0;
    const e = effect.?;
    const camera = &v.g.vs_scratch().camera;
    var delta: v.VECTOR = undefined;
    var start: v.VECTOR = undefined;
    var end: v.VECTOR = undefined;
    var offset: v.SVECTOR = undefined;
    var distance: c_int = undefined;
    start.vx = camera.lookAt.vx >> 12;
    start.vy = camera.lookAt.vy >> 12;
    start.vz = camera.lookAt.vz >> 12;
    end.vx = camera.position.vx >> 12;
    end.vy = camera.position.vy >> 12;
    end.vz = camera.position.vz >> 12;
    const matrix: *v.MATRIX = @ptrCast(&e.unk78);
    c.vs_battle_lookAt(@ptrCast(&start), @ptrCast(&end), @ptrCast(matrix));
    e.unk78.unk14.vx = start.vx;
    e.unk78.unk14.vy = start.vy;
    e.unk78.unk14.vz = start.vz;
    if (push != 0) {
        delta.vx = end.vx -% start.vx;
        delta.vy = end.vy -% start.vy;
        delta.vz = end.vz -% start.vz;
        distance = c.vs_gte_rsqrt(delta.vx *% delta.vx +% delta.vy *% delta.vy +% delta.vz *% delta.vz);
        offset.vx = 0;
        offset.vy = 0;
        offset.vz = psx.conv(i16, distance -% 0x600);
        c.SetRotMatrix(@ptrCast(matrix));
        c.SetTransMatrix(@ptrCast(matrix));
        c.RotTrans(@ptrCast(&offset), @ptrCast(&start), &distance);
        e.unk78.unk14.vx = start.vx;
        e.unk78.unk14.vy = start.vy;
        e.unk78.unk14.vz = start.vz;
    }
}
