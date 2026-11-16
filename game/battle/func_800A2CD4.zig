//! Advances a battle actor's state by its mode field (bits 28..30 of the flags
//! word at offset 8), with the shared tails of the C's gotos
//! (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A2CD4 (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Runs one step of the actor's mode machine: its state change, sound and tile bookkeeping.
pub fn func_800A2CD4(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const a = arg0.?;
    const mode: c_int = @as(c_int, psx.getBits(&a.unk0, "unkB_4"));

    switch (mode) {
        1 => {
            _ = c.vs_actors_playActorAnimation(a.unk0.unkF, 9, 0, 8);
            _ = c.vs_actors_translateActorToTileAtRate(a.unk0.unkF, @ptrCast(&a.unk5EC), -1);
            _ = c.vs_actors_turnActorToAtRate(a.unk0.unkF, a.unk5EC.pad, 8);
            a.unk5CA = a.unk0.unk1A;
            a.unk0.unk1A = 0;
            if (a.unk5CA != 0) {
                psx.setBits(&a.unk0, "unkB_4", 2);
                return;
            }
            block10(a);
        },
        2 => {
            _ = c.vs_actors_stepActorTransitionDisplacement(@ptrCast(a));
            _ = c.vs_field_movingPlatformContactHeight(@as(c_int, a.unk0.position.vx), @as(c_int, a.unk0.position.vy), @as(c_int, a.unk0.position.vz), 2);
            if (psx.Global(c_int, "D_800F49F4").ptr().* != 0) {
                psx.setBits(&a.unk0, "unkA_5", 1);
                block17(a);
                return;
            }
            if (a.unk5CA == 0) {
                _ = c.vs_actors_playActorAnimation(a.unk0.unkF, 0xA, 0, 8);
                psx.setBits(&a.unk0, "unkA_5", 0);
                psx.setBits(&a.unk0, "unkB_4", 3);
                return;
            }
        },
        3 => {
            // The word at 0x119C has no view field: read it at its offset.
            if (a.unk0.unk1A == 0 and (psx.fieldAt(c_uint, &a.unkC54._bits_unk548_0, 0).* & 0x20000) != 0) {
                block10(a);
            }
        },
        4 => {
            _ = c.vs_actors_playActorAnimation(a.unk0.unkF, 7, 0, 8);
            _ = c.vs_actors_queueActorAnimation(a.unk0.unkF, 0x25, 8, 0);
            _ = c.vs_actors_translateActorToOverTime(a.unk0.unkF, @ptrCast(&a.unk5EC), -1);
            _ = c.vs_actors_turnActorToAtRate(a.unk0.unkF, a.unk5EC.pad, 8);
            const temp: u16 = a.unk0.unk1A;
            a.unk0.unk1A = 0;
            a.unk5CA = temp;
            psx.setBits(&a.unk0, "unkA_5", 1);
            if (a.unk5CA == 0) {
                a.unk6E0 = 0;
                block17(a);
                return;
            }
            psx.setBits(&a.unk0, "unkB_4", 5);
        },
        5 => {
            _ = c.vs_actors_stepActorTransitionDisplacement(@ptrCast(a));
            _ = c.vs_field_movingPlatformContactHeight(@as(c_int, a.unk0.position.vx), @as(c_int, a.unk0.position.vy), @as(c_int, a.unk0.position.vz), 3);
            if (psx.Global(c_int, "D_800F49F4").ptr().* == 0) {
                if (a.unk5CA == 0) {
                    block17(a);
                }
            } else {
                block17(a);
            }
        },
        6 => {
            const anim: u8 = a.animationId;
            if (anim != 0xC7 and anim != 0xC9 and a.unk5CC == 0) {
                block21(a);
            }
        },
        else => {},
    }
}

/// orig: block_10 (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c, label inside func_800A2CD4)
/// Common tail of modes 1 and 3: clears the flag, runs the tile calls and sets mode 0.
fn block10(a: *v.D_800F4538_t) void {
    psx.setBits(&a.unk0, "unkA_5", 0);
    c.vs_actors_saveActorGroundedTransform(@ptrCast(a));
    c.vs_battle_aiReleaseAiDestinationTile(@as(c_int, a.unk0.currentTileX), @as(c_int, a.unk0.currentTileZ));
    _ = c.vs_actors_playActorAnimation(a.unk0.unkF, 1, 0, 8);
    psx.setBits(&a.unk0, "unkB_4", 0);
}

/// orig: block_16/block_17 (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c, labels inside func_800A2CD4)
/// Common tail of modes 2, 4 and 5: a sound call and mode 0.
fn block17(a: *v.D_800F4538_t) void {
    _ = c.vs_actors_playActorAnimation(a.unk0.unkF, 1, 0, 8);
    psx.setBits(&a.unk0, "unkB_4", 0);
}

/// orig: block_21 (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c, label inside func_800A2CD4)
/// Clears the mode field of the flags word.
fn block21(a: *v.D_800F4538_t) void {
    psx.setBits(&a.unk0, "unkB_4", 0);
}
