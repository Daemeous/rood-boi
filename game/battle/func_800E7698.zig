//! Advances a radial motion one frame: aims it at the centre and steps the state along its circle
//! (pc/src/game/BATTLE/BATTLE.PRG/7EE98.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7EE98");
const c = @import("c_7EE98");

/// orig: func_800E7698 (pc/src/game/BATTLE/BATTLE.PRG/7EE98.c)
/// Advances a radial motion one frame; returns 1 when the motion has finished.
pub fn func_800E7698(state: ?*v.vs_battle_aiState) callconv(.c) c_int {
    const st = state.?;
    const context = psx.get(st.object.D_800F4538_t).?;
    const motion: *v.radialMotion = @ptrCast(v.g.D_800F5920().*.?);
    st.count470 = 0;
    psx.setBits(context, "unk5B0_4", @intFromBool(motion.frame < 14)); // the active flag
    _ = c.vs_core_mathSquaredVectorLength(@as(c_int, 0x7C0) -% @as(c_int, st.position.v.vx), 0, @as(c_int, 0x7C0) -% @as(c_int, st.position.v.vz));
    switch (motion.state) {
        0 => {
            motion.flagA = 1;
            var angle: c_int = c.ratan2(@as(c_int, st.position.v.vx) -% 0x7C0, @as(c_int, st.position.v.vz) -% 0x7C0) & 0xFFF;
            angle = -%angle;
            angle +%= 0xC00;
            motion.angle = psx.conv(i16, angle & 0xFFF);
            motion.frame = 0;
            motion.state +%= 1;
            motion.x = st.position.v.vx;
            motion.z = st.position.v.vz;
        },
        1 => {
            if (v.g.D_800F17DC()[motion.frame][0] == 0 and v.g.D_800F17DC()[motion.frame][1] == 0) {
                const actor: *v.radialMotionActor = @ptrCast(v.g.vs_actors_actorRecords()[0].?);
                if ((actor.flags & 0x70000) != 0) {
                    c.vs_battle_aiRecoverMovement(@ptrCast(st));
                    return 0;
                }
                motion.state = 0;
                var angle: c_int = c.ratan2(@as(c_int, 0x7C0) -% @as(c_int, st.position.v.vx), @as(c_int, 0x7C0) -% @as(c_int, st.position.v.vz)) & 0xFFF;
                angle = -%angle;
                angle +%= 0xC00;
                motion.facing = psx.conv(i16, angle & 0xFFF);
                psx.setBits(context, "unk5B0_4", 0);
                return 1;
            }
        },
        else => {},
    }
    var angle: c_int = c.rcos(@as(c_int, motion.angle));
    st.moveX = (@as(c_int, st.position.v.vx) << 12) +% (@as(c_int, v.g.D_800F17DC()[motion.frame][0]) *% angle);
    angle = c.rsin(@as(c_int, motion.angle));
    st.moveZ = (@as(c_int, st.position.v.vz) << 12) +% (@as(c_int, v.g.D_800F17DC()[motion.frame][0]) *% angle);
    st.moveY = (@as(c_int, st.position.v.vy) +% @as(c_int, v.g.D_800F17DC()[motion.frame][1])) << 12;
    c.vs_battle_aiIssueActorCommand(@ptrCast(st), 8);
    motion.flag3 = 0;
    motion.frame +%= 1;
    return 0;
}
