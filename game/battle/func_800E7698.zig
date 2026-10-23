//! Advances a radial motion one frame: aims it at the centre and steps the state along its circle
//! (src/BATTLE/BATTLE.PRG/7EE98.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7EE98");
const c = @import("c_7EE98");

/// orig: func_800E7698 (src/BATTLE/BATTLE.PRG/7EE98.c)
/// Advances a radial motion one frame; returns 1 when the motion has finished.
pub fn func_800E7698(state: ?*v.radialMotionState) callconv(.c) c_int {
    const st = state.?;
    const context = psx.get(st.context).?;
    const motion: *v.radialMotion = @ptrCast(v.g.D_800F5920().*.?);
    st.field470 = 0;
    psx.setBits(context, "active", @intFromBool(motion.frame < 14));
    _ = c.func_800E4660(@as(c_int, 0x7C0) -% @as(c_int, st.x), 0, @as(c_int, 0x7C0) -% @as(c_int, st.z));
    switch (motion.state) {
        0 => {
            motion.flagA = 1;
            var angle: c_int = c.ratan2(@as(c_int, st.x) -% 0x7C0, @as(c_int, st.z) -% 0x7C0) & 0xFFF;
            angle = -%angle;
            angle +%= 0xC00;
            motion.angle = psx.conv(i16, angle & 0xFFF);
            motion.frame = 0;
            motion.state +%= 1;
            motion.x = st.x;
            motion.z = st.z;
        },
        1 => {
            if (v.g.D_800F17DC()[motion.frame][0] == 0 and v.g.D_800F17DC()[motion.frame][1] == 0) {
                const actor: *v.radialMotionActor = @ptrCast(v.g.D_800F4538()[0].?);
                if ((actor.flags & 0x70000) != 0) {
                    c.func_800E2CCC(@ptrCast(st));
                    return 0;
                }
                motion.state = 0;
                var angle: c_int = c.ratan2(@as(c_int, 0x7C0) -% @as(c_int, st.x), @as(c_int, 0x7C0) -% @as(c_int, st.z)) & 0xFFF;
                angle = -%angle;
                angle +%= 0xC00;
                motion.facing = psx.conv(i16, angle & 0xFFF);
                psx.setBits(context, "active", 0);
                return 1;
            }
        },
        else => {},
    }
    var angle: c_int = c.rcos(@as(c_int, motion.angle));
    st.targetX = (@as(c_int, st.x) << 12) +% (@as(c_int, v.g.D_800F17DC()[motion.frame][0]) *% angle);
    angle = c.rsin(@as(c_int, motion.angle));
    st.targetZ = (@as(c_int, st.z) << 12) +% (@as(c_int, v.g.D_800F17DC()[motion.frame][0]) *% angle);
    st.targetY = (@as(c_int, st.y) +% @as(c_int, v.g.D_800F17DC()[motion.frame][1])) << 12;
    c.func_800DEEFC(@ptrCast(st), 8);
    motion.flag3 = 0;
    motion.frame +%= 1;
    return 0;
}
