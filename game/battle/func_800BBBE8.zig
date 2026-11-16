//! Battle camera setup: allocates the camera block and resets its eye, look-at and projection state (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The camera block allocated by func_800BBBE8, re-read at each store as the C does.
fn cam() *v.D_800F4BA4_t {
    return v.g.vs_event_camera().*.?;
}

/// orig: _copyVector (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Copies the three components of a vector; the padding word of the destination is left alone, as copyVector does.
fn copyVector(dst: *v.VECTOR, src: *const v.VECTOR) void {
    dst.vx = src.vx;
    dst.vy = src.vy;
    dst.vz = src.vz;
}

/// orig: func_800BBBE8 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Allocates the battle camera block and resets it to the default eye, look-at and projection state.
pub fn func_800BBBE8() callconv(.c) void {
    var sp20: v.VECTOR = undefined;
    var sp30: v.VECTOR = undefined;

    psx.put(v.g.vs_event_camera(), c.vs_main_allocHeapR(@as(c_uint, @sizeOf(v.D_800F4BA4_t))));
    v.g.vs_event_skipNextFieldCameraRestore().* = 0;

    c.vs_battle_getCameraPosition(psx.cptr(c.VECTOR, &sp20));
    c.vs_battle_getCameraLookAt(psx.cptr(c.VECTOR, &sp30));
    copyVector(&cam().eye.current, &sp20);
    copyVector(&cam().lookAt.target, &sp20);
    copyVector(&cam().lookAt.current, &sp30);
    copyVector(&cam().eye.target, &sp30);

    cam().lookAt.mode = 0xF;
    cam().lookAt.follow = 0;
    cam().lookAt.following = 0;
    cam().eye.mode = 0xF;
    cam().eye.follow = 0;
    cam().eye.following = 0;
    cam().unk168.durationFrames = 0;
    cam().unk168.angle = 0;
    cam().unk168.hasChanged = 1;

    c.vs_render_setFramebufferEffectEnabled(0);

    cam().unk174.durationFrames = 0;
    cam().unk174.angle = 0;

    c.vs_render_setFramebufferEffectRoll(0);

    cam().unk174.hasChanged = 1;
    cam().screenScale.currentValue1 = 4096;
    cam().screenScale.currentValue0 = 4096;

    c.vs_render_setFramebufferEffectScale(psx.cptr(c.D_800F1A68_t, v.g.D_800E9C0C()));

    cam().screenTint.currentValue0 = 0;
    cam().screenTint.currentValue1 = 0;
    cam().screenTint.currentValue2 = 0;
    cam().screenScale.durationFrames = 0;

    c.vs_render_setFramebufferEffectTint(psx.cptr(c.P_CODE, v.g.vs_event_defaultScreenTint()));

    cam().screenTint.durationFrames = 0;
    cam().screenTranslation.currentValue1 = 4096;
    cam().screenTranslation.currentValue0 = 4096;

    c.vs_render_setFramebufferEffectOffset(psx.cptr(c.D_800F1A68_t, v.g.D_800E9C0C()));

    cam().screenTranslation.durationFrames = 0;

    c.vs_render_setFramebufferEffectBlendMode(1);

    cam().unk25C.currentValue0 = 4096;
    cam().unk25C.durationFrames = 0;
    cam().unk25C.currentValue1 = 0;
    cam().projectionDistance.value = 0x200;
    cam().projectionDistance.durationFrames = 0;
    cam().projectionDistance.hasChanged = 1;
    cam().nearClip.value = 0x40;
    cam().nearClip.durationFrames = 0;
    cam().nearClip.hasChanged = 1;
    cam().farClip.value = 0x1000;
    cam().farClip.durationFrames = 0;
    cam().farClip.hasChanged = 1;
    cam().unk1A4.unk0.s16 = 0;
    cam().unk1A4.unkB = 0;
    cam().unk1B0.unkB = 0;
    cam().originActorId = 0x2000;
}
