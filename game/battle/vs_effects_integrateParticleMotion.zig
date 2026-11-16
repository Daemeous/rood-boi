//! Effect motion integration over a linked list of effect nodes (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_integrateParticleMotion (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Steps every effect node in the list one tick: drag and gravity, then attraction toward its target.
pub fn vs_effects_integrateParticleMotion(node: ?*v.func_800D2904_t) callconv(.c) void {
    const drag: c_int = v.g.vs_effects_particleDrag().*;
    const gravityX: c_int = v.g.vs_effects_particleGravity().x;
    const gravityY: c_int = v.g.vs_effects_particleGravity().y;
    const gravityZ: c_int = v.g.vs_effects_particleGravity().z;

    var cur = node;
    while (cur) |n| {
        // state = (effectMotionState*)&node->unk8
        const state = psx.fieldAt(v.effectMotionState, &n.unk8, 0);
        var damping: c_int = @as(c_int, n.unk8) -% drag;

        state.vx = @divTrunc(state.vx *% damping +% (state.fx << 12), @as(c_int, n.unk8)) +%
            ((gravityX *% @as(c_int, state.gravityScale)) >> 12);
        state.x +%= state.vx;

        state.vy = @divTrunc(state.vy *% damping +% (state.fy << 12), @as(c_int, n.unk8)) +%
            ((gravityY *% @as(c_int, state.gravityScale)) >> 12);
        state.y +%= state.vy;

        state.vz = @divTrunc(state.vz *% damping +% (state.fz << 12), @as(c_int, n.unk8)) +%
            ((gravityZ *% @as(c_int, state.gravityScale)) >> 12);
        state.z +%= state.vz;

        damping = n.unk70; // the attraction; its target is unk62[0..2]
        if (damping != 0) {
            var direction: v.VECTOR = undefined;
            direction.vx = @as(c_int, n.unk62[0]) -% (state.x >> 12);
            direction.vy = @as(c_int, n.unk62[1]) -% (state.y >> 12);
            direction.vz = @as(c_int, n.unk62[2]) -% (state.z >> 12);
            if (direction.vx != 0 or direction.vy != 0 or direction.vz != 0) {
                _ = c.VectorNormal(psx.cptr(c.VECTOR, &direction), psx.cptr(c.VECTOR, &direction));
                direction.vx = (direction.vx *% damping) >> 12;
                direction.vy = (direction.vy *% damping) >> 12;
                direction.vz = (direction.vz *% damping) >> 12;
            }
            state.fx +%= state.ax +% direction.vx;
            state.fy +%= state.ay +% direction.vy;
            state.fz +%= state.az +% direction.vz;
        } else {
            state.fx +%= state.ax;
            state.fy +%= state.ay;
            state.fz +%= state.az;
        }

        cur = psx.get(n.next);
    }
}
