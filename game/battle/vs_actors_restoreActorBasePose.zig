//! Ends an actor's move state, or clears a stalled object's state (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_actors_restoreActorBasePose (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Resets actor `actorId` after its move (a reaction step, or a stalled move's end); with no actor, clears the object's state.
pub fn vs_actors_restoreActorBasePose(actorId: c_int, animation: c_int) callconv(.c) void {
    const actor = psx.at(v.g.vs_actors_actorRecords(), actorId).*;
    if (actor) |a| {
        const currentAnimation: c_int = a.animationId;
        if (currentAnimation != 0xC6 and currentAnimation != 0xC8 and
            (a.unk0.slot != 0 or (psx.bitsAt(u32, &a.unk0._bits_skip, 0).* & 0x184000) != 0x80000))
        {
            const flags = psx.bitsAt(u32, &a.unk0._bits_skip, 0).*;
            const state = flags & 0x70000000;
            if (((flags >> 16) & 7) < 2 and state != 0x20000000 and state != 0x50000000) {
                const action = flags & 0x0F000000;
                var skipMove = false;
                if (action == 0x3000000) {
                    _ = c.func_800A2C48(@ptrCast(a));
                } else if (action != 0) {
                    // goto resetState
                    skipMove = true;
                }
                if (!skipMove) {
                    const movementFlags = psx.bitsAt(u32, &a.unk0._bits_skip, 0).*;
                    if ((movementFlags & 0x180000) == 0x100000) {
                        if (psx.fieldAt(u32, &a.unk0.turnFrames, 0).* != 0) {
                            a.unk0.translationFrames = 0;
                            a.unk0.turnFrames = 0;
                            a.unk0.position.vx = psx.conv(i16, @as(c_int, psx.conv(u16, a.unk0.position.vx)) +%
                                @as(c_int, psx.conv(u16, a.unk0.unk34.vx)));
                            a.unk0.position.vy = psx.conv(i16, @as(c_int, psx.conv(u16, a.unk0.position.vy)) +%
                                @as(c_int, psx.conv(u16, a.unk0.unk34.vy)));
                            a.unk0.position.vz = psx.conv(i16, @as(c_int, psx.conv(u16, a.unk0.position.vz)) +%
                                @as(c_int, psx.conv(u16, a.unk0.unk34.vz)));
                            a.unk0.facing = psx.conv(i16, @as(c_int, psx.conv(u16, a.unk0.facing)) +%
                                @as(c_int, psx.conv(u16, a.unk0.facingDelta)));
                        }
                    } else if ((movementFlags & 0xF00) == 0) {
                        var baseAnimation: c_int = a.animationId;
                        if (baseAnimation >= 0x65) {
                            baseAnimation -%= 0x64;
                        }
                        if (baseAnimation != 0x62) {
                            var distance: c_int = 1;
                            if (a.unk0.slot == 0) {
                                const reactionAnimation: c_int = a.animationId;
                                var move = false;
                                if (psx.conv(c_uint, reactionAnimation -% 0x4F) >= 4) {
                                    if (psx.conv(c_uint, reactionAnimation -% 0x47) >= 4 and reactionAnimation != 0x4C) {
                                        // no move from this reaction
                                    } else {
                                        move = true;
                                    }
                                } else {
                                    distance = 2;
                                    move = true;
                                }
                                if (move) {
                                    // moveFromReaction: facing is masked to 0..0xFFF, so the division is non-negative.
                                    const facing: c_int = (@as(c_int, a.unk0.facing) +% 0x200) & 0xFFF;
                                    const direction = if (actorId == 0) common.analog.cardinal(facing) else @divTrunc(facing, 1024); // PC: with hysteresis (analog_move.c)
                                    var tileDx: c_int = 0;
                                    var tileDz: c_int = 0;
                                    if (direction < 2) {
                                        distance = -%distance;
                                    }
                                    if ((direction & 1) != 0) {
                                        tileDx = distance;
                                    } else {
                                        tileDz = distance;
                                    }
                                    _ = c.vs_battle_aiReleaseAiDestinationTile(
                                        @as(c_int, a.unk0.currentTileX) +% tileDx,
                                        @as(c_int, a.unk0.currentTileZ) +% tileDz,
                                        tileDx,
                                        tileDz,
                                    );
                                }
                            }
                            if (psx.getBits(a, "unk5AC_3") != 0) {
                                _ = c.vs_actors_playActorAnimation(@as(c_int, a.unk0.slot), 0x4B, 0, 0xC);
                            } else {
                                _ = c.vs_actors_playActorAnimation(actorId, animation, 0, 0x10);
                                a.unk1846 = 0;
                            }
                        }
                    }
                }
            }
            // resetState:
            if ((psx.bitsAt(u32, &a.unk0._bits_skip, 0).* & 0x180000) != 0x100000) {
                psx.bitsAt(u32, &a.unk0._bits_skip, 0).* &= 0xFFE7FFFF;
            }
            a.scaleFrames = 0;
            a.unk0.turnFrames = 0;
            a.unk0.translationFrames = 0;
            psx.bitsAt(u32, &a.unk0._bits_skip, 0).* &= 0xFFFFBFFF;
            _ = c.vs_actors_resetActorRunMomentum(actorId);
        }
    } else {
        const object = psx.at(v.g.vs_field_gimmickInstances(), actorId).*;
        if (object) |obj| {
            const objectState: c_int = obj.unk1A;
            if ((objectState == 0xF9 or objectState == 0xFF) and obj.unk6C[8].unk4 == 0) {
                obj.unk1A = 0;
            }
        }
    }
}
