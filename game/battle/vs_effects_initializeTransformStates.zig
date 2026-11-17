//! Builds the effect transform states of an effect from its definitions
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_initializeTransformStates (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Fills each effect transform state from its definition: rotation, the actor and bone it is attached to, and its target.
pub fn vs_effects_initializeTransformStates(arg0: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    var i: c_int = 0;
    while (i < @as(c_int, psx.get(v.g.vs_effects_package().*.?.block9Data).?.unk15)) : (i +%= 1) {
        const ctx = arg0.?;
        const raw10: *align(4) anyopaque = @alignCast(psx.get(v.g.vs_effects_package().*.?.block10Data).?);
        const definitions: [*]v.func_800D0B30_t = @ptrCast(raw10);
        const definition: *v.func_800D0B30_t = &definitions[@intCast(i)];
        const unk1C: [*]v.func_800CE714_t2_2 = @ptrCast(&ctx.unk1C);
        const state: *v.effectTransformState = @ptrCast(&unk1C[@intCast(i)]);

        state.rotation[0] = psx.conv(i16, definition.unk8);
        state.rotation[1] = psx.conv(i16, definition.unkC);
        state.rotation[2] = psx.conv(i16, definition.unk10);
        state.position.vx = 0;
        state.position.vy = 0;
        state.position.vz = 0;
        c.vs_core_mathIdentityRotation(@ptrCast(&state.facing));
        state.flags = 0;
        state.frame = 0;
        @as(*align(1) c_int, @ptrCast(&state.actor)).* = 0;

        if ((@as(c_int, definition.unk0) & 2) != 0) {
            switch (@as(c_int, definition.unk0) & 0x18) {
                0x10 => {
                    if (ctx.context.unk24.unk0 == 4) {
                        state.actor = psx.fieldAt(u8, &ctx.context.unk24.unk4, 0).*;
                        state.bone = psx.fieldAt(u8, &ctx.context.unk24.unk4, 2).*;
                    } else {
                        state.actor = 0xFF;
                        c.vs_core_mathTranslationTransform(@ptrCast(&ctx.context.unk24.unk4), @ptrCast(&state.attachment));
                    }
                },
                8 => {
                    if (ctx.context.unkC.unk0 == 4) {
                        state.actor = psx.fieldAt(u8, &ctx.context.unkC.unk4, 0).*;
                        state.bone = psx.fieldAt(u8, &ctx.context.unkC.unk4, 2).*;
                    } else {
                        state.actor = 0xFF;
                        c.vs_core_mathTranslationTransform(@ptrCast(&ctx.context.unkC.unk4), @ptrCast(&state.attachment));
                    }
                },
                0 => {
                    state.actor = psx.conv(u8, definition.unk2);
                    state.bone = 0;
                },
                else => {},
            }
            if ((@as(c_int, definition.unk0) & 4) == 0)
                state.bone = psx.byteAt(u8, definition, 3).*;
        }

        if ((@as(c_int, definition.unk0) & 0x80) != 0) {
            switch (@as(c_int, definition.unk0) & 0xC00) {
                0x800 => {
                    if (ctx.context.unk24.unk0 == 4) {
                        state.targetActor = psx.fieldAt(u8, &ctx.context.unk24.unk4, 0).*;
                        state.targetBone = psx.fieldAt(u8, &ctx.context.unk24.unk4, 2).*;
                    } else {
                        state.targetActor = 0xFF;
                        state.saved = @as(*const v.SVECTOR, @ptrCast(&ctx.context.unk24.unk4)).*;
                    }
                },
                0x400 => {
                    if (ctx.context.unkC.unk0 == 4) {
                        state.targetActor = psx.fieldAt(u8, &ctx.context.unkC.unk4, 0).*;
                        state.targetBone = psx.fieldAt(u8, &ctx.context.unkC.unk4, 2).*;
                    } else {
                        state.targetActor = 0xFF;
                        state.saved = @as(*const v.SVECTOR, @ptrCast(&ctx.context.unkC.unk4)).*;
                    }
                },
                0 => {
                    state.targetActor = psx.byteAt(u8, definition, 10).* & 31;
                    state.targetBone = 0;
                },
                else => {},
            }
            if ((@as(c_int, definition.unk0) & 0x200) == 0)
                state.targetBone = psx.byteAt(u8, definition, 11).*;
        }
    }
}
