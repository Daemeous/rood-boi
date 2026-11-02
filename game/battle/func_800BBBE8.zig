//! Battle camera setup: allocates the camera block and resets its eye, look-at and projection state (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The camera block allocated by func_800BBBE8, re-read at each store as the C does.
fn cam() *v.D_800F4BA4_t {
    return v.g.D_800F4BA4().*.?;
}

/// orig: _copyVector (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Copies the three components of a vector; the padding word of the destination is left alone, as copyVector does.
fn copyVector(dst: *v.VECTOR, src: *const v.VECTOR) void {
    dst.vx = src.vx;
    dst.vy = src.vy;
    dst.vz = src.vz;
}

/// orig: func_800BBBE8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Allocates the battle camera block and resets it to the default eye, look-at and projection state.
pub fn func_800BBBE8() callconv(.c) void {
    var sp20: v.VECTOR = undefined;
    var sp30: v.VECTOR = undefined;

    psx.put(v.g.D_800F4BA4(), c.vs_main_allocHeapR(@as(c_uint, @sizeOf(v.D_800F4BA4_t))));
    v.g.D_800F4BA0().* = 0;

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
    cam().unk168.unkA = 0;
    cam().unk168.unk2 = 0;
    cam().unk168.unk0 = 1;

    c.func_8007DD50(0);

    cam().unk174.unkA = 0;
    cam().unk174.unk2 = 0;

    c.func_8007DDAC(0);

    cam().unk174.unk0 = 1;
    cam().unk1FC.unk4 = 4096;
    cam().unk1FC.unk2 = 4096;

    c.func_8007DDB8(psx.cptr(c.D_800F1A68_t, v.g.D_800E9C0C()));

    cam().unk21C.unk2 = 0;
    cam().unk21C.unk4 = 0;
    cam().unk21C.unk6 = 0;
    cam().unk1FC.unk1E = 0;

    c.func_8007DDD4(psx.cptr(c.P_CODE, v.g.D_800E9C08()));

    cam().unk21C.unk1E = 0;
    cam().unk23C.unk4 = 4096;
    cam().unk23C.unk2 = 4096;

    c.func_8007DDF8(psx.cptr(c.D_800F1A68_t, v.g.D_800E9C0C()));

    cam().unk23C.unk1E = 0;

    c.func_8007DE2C(1);

    cam().unk25C.unk2 = 4096;
    cam().unk25C.unk1E = 0;
    cam().unk25C.unk4 = 0;
    cam().projectionDistance.unk2 = 0x200;
    cam().projectionDistance.unk9 = 0;
    cam().projectionDistance.unk0 = 1;
    cam().nearClip.unk2 = 0x40;
    cam().nearClip.unk9 = 0;
    cam().nearClip.unk0 = 1;
    cam().farClip.unk2 = 0x1000;
    cam().farClip.unk9 = 0;
    cam().farClip.unk0 = 1;
    cam().unk1A4.unk0.s16 = 0;
    cam().unk1A4.unkB = 0;
    cam().unk1B0.unkB = 0;
    cam().unk1E8 = 0x2000;
}
