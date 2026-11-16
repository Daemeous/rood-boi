//! Battle load and scene state machine step, driven by vs_effects_primaryPhase (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_updatePrimaryEffects (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs one step of the battle load state machine; returns 1 while it is still busy, 0 when done.
pub fn vs_effects_updatePrimaryEffects() callconv(.c) c_int {
    var var_s0: ?*v.D_800F53B8_t = v.g.vs_effects_primaryInstances().*;
    var var_s2: c_int = 1;

    sw: switch (v.g.vs_effects_primaryPhase().*) {
        0 => {
            var_s2 = 0;
        },
        6 => {
            if (c.vs_battle_loadEffPurge() == 0) {
                c.vs_main_wait();
                c.vs_effpurge_exec();
                if (v.g.vs_effects_actionRequest().effectId != 0 and v.g.vs_battle_pFileLoadContext().lba == 0) {
                    c.vs_effects_setPrimaryEffectPhase(4);
                    v.g.vs_effects_trackCount().* = 0;
                    v.g.vs_effects_startedTrackCount().* = 0;
                } else {
                    c.vs_effects_setPrimaryEffectPhase(2);
                }
                if (v.g.D_800F5684().* != 0) {
                    _ = c.func_8007E1C0(2);
                    v.g.D_800F5684().* = 0;
                }
            }
        },
        2 => {
            if (c.vs_effects_updateEffectTextureLoading() == 0) {
                c.vs_effects_setPrimaryEffectPhase(8);
            }
        },
        8 => {
            if (c.vs_effects_updateEffectPluginLoading() == 0) {
                c.vs_effects_setPrimaryEffectPhase(3);
                c.vs_main_wait();
            }
        },
        3 => {
            if (c.vs_effects_loadEffectPackage() == 0) {
                c.vs_effects_bindEffectPackageBlocks(v.g.vs_battle_pfileBuf().*);
                c.vs_effects_setPrimaryEffectPhase(5);

                v.g.vs_effects_trackCount().* = @as(i32, psx.get(v.g.vs_effects_package().*.?.block9Data).?.unk14);
                v.g.vs_effects_startedTrackCount().* = 0;

                var i: c_int = 0;
                while (i < v.g.vs_effects_trackCount().*) : (i +%= 1) {
                    // x86 masks the shift count to 5 bits; the mask keeps the same bit.
                    const sh: u5 = @truncate(@as(c_uint, @bitCast(i)));
                    v.g.vs_effects_pendingTracks().* |= @as(c_uint, 1) << sh;
                }

                v.g.vs_effects_primaryLoaded().* = 1;
                var sp38: v.vs_main_CdFile = undefined;
                sp38.lba = c.vs_battle_getMainMenuLba();
                sp38.size = 0;
                v.g.D_800F5218().* = @ptrCast(c.vs_main_allocateCdQueueSlot(psx.cptr(c.vs_main_CdFile, &sp38)));
                c.vs_main_cdEnqueue(@ptrCast(v.g.D_800F5218().*), null);
            }
        },
        4 => {
            while (var_s0) |node| {
                const temp_s1 = psx.get(node.next);
                v.g.vs_effects_currentInstance().* = node;

                if (psx.get(v.g.vs_effects_package().*.?.curveBlock) != null) {
                    c.vs_effects_sampleAllEffectCurves(@as(c_int, psx.getBits(node, "unk14_16")));
                }

                c.vs_effects_updateParticleGravityAndDrag();

                if (c.vs_effects_runScript(@ptrCast(node)) == 0) {
                    _ = c.vs_effects_destroyPrimaryEffectInstance(@ptrCast(node));
                } else {
                    psx.setBits(node, "unk14_16", psx.conv(u16, @as(c_int, psx.getBits(node, "unk14_16")) +% 1));
                }

                var_s0 = temp_s1;
            }

            if (v.g.vs_effects_primaryInstances().* != null) break :sw;

            if (v.g.vs_effects_trackCount().* <= v.g.vs_effects_startedTrackCount().*) {
                if (psx.get(v.g.vs_effects_package().*.?.block12Data) != null) {
                    _ = c.vs_audio_unbindRoomSfxBank();
                    psx.put(&v.g.vs_effects_package().*.?.block12Data, null);
                }

                if (psx.get(v.g.vs_effects_package().*.?.block13Data) != null) {
                    _ = c.vs_audio_unbindRoomCutsceneStream();
                    psx.put(&v.g.vs_effects_package().*.?.block13Data, null);
                    v.g.vs_effects_package().*.?.unkCC = 0;
                }

                _ = c.vs_input_stopVibrationPatterns(0, 1);
                c.vs_effects_releaseEffectPackageResources();
                c.vs_effects_releaseParticlePool();
                c.vs_effects_releaseEffectToken(@as(c_int, v.g.vs_effects_primaryToken().*));

                v.g.vs_effects_primaryLoaded().* = 0;
                v.g.vs_effects_primaryToken().* = 0;

                if (v.g.D_800F5684().* != 0) {
                    _ = c.func_8007E1C0(2);
                    v.g.D_800F5684().* = 0;
                }

                c.vs_effects_resetEffectCameraMotion();
                c.vs_effects_setPrimaryEffectPhase(9);
            } else {
                c.vs_effects_setPrimaryEffectPhase(5);
            }

            var_s2 = 1;
        },
        9 => {
            if (v.g.D_800F5218().*) |slot| {
                if (slot.state != 4) {
                    var_s2 = 1;
                    break :sw;
                }
                c.vs_main_freeCdQueueSlot(@ptrCast(slot));
                v.g.D_800F5218().* = null;
            }
            c.vs_effects_setPrimaryEffectPhase(0);
            var_s2 = 0;
        },
        else => {},
    }

    return var_s2;
}
