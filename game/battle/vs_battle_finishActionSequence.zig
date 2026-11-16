//! After an action: the camera eases back, then the hits' reactions play (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// vs_battle_actionState, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.vs_battle_actionState().*.?;
}

/// The chain link of `k` (`k % 4` as the C's decompiled signed form computes it).
fn link(k: c_int) *v.D_800F19CC_t2 {
    var rounded = k;
    if (k < 0) {
        rounded = k +% 3;
    }
    return &state().queue[@intCast(k -% (rounded >> 2) *% 4)];
}

/// orig: vs_battle_finishActionSequence (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// While D_800F19EC is 1 or 3, eases the projection distance to 512 (by 64) and the camera distance to unk2C0C (by
/// 192). Once both are there (D_800F19D8 too) and the look-at has arrived, and no hit actor of any chain link is
/// still busy (vs_actors_isActorActionAnimationBusy), plays every hit's reaction and every link user's own (vs_battle_resolveActorDefeat), then, unless
/// in battle state 3, ends the action (func_80073898 when vs_event_evaluateRoomEventTriggers(0xC) answers 1, else vs_items_openPendingEnemyLoot).
pub fn vs_battle_finishActionSequence() callconv(.c) void {
    const mode = psx.Global(c_int, "D_800F19EC").ptr().*;
    const projection = v.g.vs_main_projectionDistance();
    const camera = &v.g.vs_battle_cameraCurrentSpherical().values;
    if (mode == 1 or mode == 3) {
        if (projection.* > 512) {
            projection.* -%= 64;
            if (projection.* <= 512) {
                projection.* = 512;
            }
            c.SetGeomScreen(projection.*);
        } else if (projection.* < 512) {
            projection.* +%= 64;
            if (projection.* >= 512) {
                projection.* = 512;
            }
            c.SetGeomScreen(projection.*);
        }
        if (camera.distance < state().unk2C0C) {
            camera.distance +%= 192;
            if (camera.distance > state().unk2C0C) {
                camera.distance = state().unk2C0C;
            }
        } else if (camera.distance > state().unk2C0C) {
            camera.distance -%= 192;
            if (camera.distance < state().unk2C0C) {
                camera.distance = state().unk2C0C;
            }
        }
    }
    if (projection.* == 0x200 and psx.Global(c_int, "D_800F19D8").ptr().* == state().unk2C0C and c._isLookAtAtDestination() != 0) {
        var idle = true;
        var k: c_int = 0;
        // unk4C has 30 entries; the hit count unk4A is not bounded by the C.
        if (state().unk4 != 0) {
            while (true) {
                const chain = link(k);
                var h: c_int = 0;
                if (chain.targetCount != 0) {
                    while (true) {
                        const hit = psx.at(&chain.targets, h);
                        if (hit.unk40 == 0 and c.vs_actors_isActorActionAnimationBusy(hit.target.targetActor) +% 1 >= 2) {
                            idle = false;
                        }
                        h += 1; // h < a u16
                        if (!(h < chain.targetCount)) break;
                    }
                }
                k += 1; // k < a u32
                if (!(psx.conv(c_uint, k) < state().unk4)) break;
            }
        }
        if (idle) {
            c.vs_field_resumeFieldAfterMenu();
            c.vs_battle_setHudHidden(0);
            k = 0;
            c.func_8009E5C4(state().unk8.primaryTarget.target.targetActor);
            if (state().unk4 != 0) {
                while (true) {
                    const chain = link(k);
                    var h: c_int = 0;
                    if (chain.targetCount != 0) {
                        while (true) {
                            if (psx.at(&chain.targets, h).unk40 == 0) {
                                _ = c.vs_battle_resolveActorDefeat(chain.primaryTarget.target.targetActor, psx.at(&chain.targets, h).target.targetActor, 1);
                            }
                            h += 1; // h < a u16
                            if (!(h < chain.targetCount)) break;
                        }
                    }
                    k += 1; // k < a u32
                    if (!(psx.conv(c_uint, k) < state().unk4)) break;
                }
            }
            k = 0;
            if (state().unk4 != 0) {
                while (true) {
                    const chain = link(k);
                    if (chain.primaryTarget.unk40 == 0) {
                        const user = chain.primaryTarget.target.targetActor;
                        _ = c.vs_battle_resolveActorDefeat(user, user, 1);
                    }
                    k += 1; // k < a u32
                    if (!(psx.conv(c_uint, k) < state().unk4)) break;
                }
            }
            if (v.g.D_800F196C().* != 3) {
                if (c.vs_event_evaluateRoomEventTriggers(0xC, 0, 0, 0) == 1) {
                    c.func_80073898();
                    return;
                }
                c.vs_items_openPendingEnemyLoot();
            }
        }
    }
}
