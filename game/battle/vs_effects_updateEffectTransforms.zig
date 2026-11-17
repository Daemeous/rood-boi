//! Per-frame transform update of the battle's effect instances (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_updateEffectTransforms (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Updates each effect transform: its attachment, world matrix, look-at and view matrix.
pub fn vs_effects_updateEffectTransforms(arg0: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    const scratch: *v.func_800D0B30_t2 = pad.at(v.func_800D0B30_t2, 0x2C8);
    var i: c_int = 0;
    while (i < @as(c_int, psx.get(v.g.vs_effects_package().*.?.block9Data).?.unk15)) : (i +%= 1) {
        const defs: [*]v.func_800D0B30_t = @ptrCast(@alignCast(psx.get(v.g.vs_effects_package().*.?.block10Data).?));
        const definition = psx.at(defs, i);
        const state: *v.effectTransformState = @ptrCast(@alignCast(psx.at(&arg0.?.unk1C, i)));

        c.vs_effects_buildAnimatedLocalTransform(psx.cptr(c.func_800D0B30_t, definition), @ptrCast(state), psx.cptr(c.func_800D0B30_t2, scratch));

        if ((definition.unk0 & 0x80) != 0 and state.targetActor != 0xFF and
            ((definition.unk0 & 0x100) != 0 or (state.flags & 2) == 0))
        {
            if (state.targetActor == 0x12) {
                state.saved.vx = v.g.vs_effects_actionRequest().unkE0.vx;
                state.saved.vy = v.g.vs_effects_actionRequest().unkE0.vy;
                state.saved.vz = v.g.vs_effects_actionRequest().unkE0.vz;
            } else {
                c.func_800A1AF8(@as(c_int, state.targetActor), @as(c_int, state.targetBone), @ptrCast(&state.saved), 0);
            }
            state.flags |= 2;
        }

        if (state.actor != 0xFF and ((definition.unk0 & 0x20) != 0 or (state.flags & 1) == 0)) {
            c.vs_effects_resolveTransformAttachment(@bitCast(psx.addr(arg0.?)), @ptrCast(state));
            state.flags |= 1;
        }

        if ((definition.unk0 & 2) != 0) {
            if ((definition.unk0 & 0xC000) == 0x4000) {
                const words: *align(1) [8]i32 = @ptrCast(&state.attachment);
                const cos = c.rcos(@as(c_int, state.saved.pad));
                words[4] = cos;
                words[0] = cos;
                words[1] = c.rsin(@as(c_int, state.saved.pad));
                words[3] = -%words[1];
                words[2] = 0x1000;
            }
            _ = c.CompMatrix(psx.cptr(c.MATRIX, &state.attachment), psx.cptr(c.MATRIX, &scratch.unk48), psx.cptr(c.MATRIX, &state.world));
        } else {
            state.world = scratch.unk48;
        }

        if ((definition.unk0 & 0x3000) == 0x2000 and (state.flags & 8) == 0) {
            c.vs_effects_buildTransformSplineControls(@ptrCast(definition), @ptrCast(state));
            state.flags |= 8;
        }

        if ((definition.unk0 & 0x80) != 0) {
            c.vs_effects_advanceTransformMotion(@ptrCast(definition), @ptrCast(state));
        }

        if ((definition.unk0 & 0x40) != 0) {
            state.world.t[0] = state.position.vx;
            state.world.t[1] = state.position.vy;
            state.world.t[2] = state.position.vz;
        } else {
            state.world.t[0] +%= state.position.vx;
            state.world.t[1] +%= state.position.vy;
            state.world.t[2] +%= state.position.vz;
        }

        // (short)unk0 & 0x8000 is bit 15 of unk0.
        if ((definition.unk0 & 0x8000) != 0) {
            if ((definition.unk0 & 0x4000) != 0) {
                scratch.target.vx = state.saved.vx;
                scratch.target.vy = state.saved.vy;
                scratch.target.vz = state.saved.vz;
            } else {
                scratch.target.vx = state.attachment.t[0];
                scratch.target.vy = state.attachment.t[1];
                scratch.target.vz = state.attachment.t[2];
            }
            if (scratch.target.vx != state.world.t[0] or
                scratch.target.vy != state.world.t[1] or
                scratch.target.vz != state.world.t[2])
            {
                c.vs_battle_lookAt(@ptrCast(&state.world.t), psx.cptr(c.VECTOR, &scratch.target), psx.cptr(c.MATRIX, &state.facing));
                state.facing.t[0] = state.world.t[0];
                state.facing.t[1] = state.world.t[1];
                state.facing.t[2] = state.world.t[2];
            }
            _ = c.CompMatrix(psx.cptr(c.MATRIX, &state.facing), psx.cptr(c.MATRIX, &scratch.unk48), psx.cptr(c.MATRIX, &state.world));
        }

        _ = c.CompMatrix(psx.cptr(c.MATRIX, &v.g.vs_view().viewMatrix), psx.cptr(c.MATRIX, &state.world), psx.cptr(c.MATRIX, &state.view));
    }
}
