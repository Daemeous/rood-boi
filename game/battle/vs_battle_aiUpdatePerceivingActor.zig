//! Per-frame battle actor update: timers, movement history, relations, the
//! action candidate and the alert level of each actor (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_6FC00");
const c = @import("c_6FC00");

/// How one actor's update ends: falling through, `goto skip` or `goto idle`.
const Outcome = enum { done, idle, skip };

/// orig: historyRepeats (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c)
/// True when the last two stored history samples are equal in X and Z.
inline fn historyRepeats(state: *v.vs_battle_aiState) bool {
    return state.historyX.w[0] == state.historyX.w[1] and
        state.historyZ.w[0] == state.historyZ.w[1];
}

/// orig: historyStalled (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c)
/// True when the history buffer is full and its samples have stopped changing.
inline fn historyStalled(state: *v.vs_battle_aiState) bool {
    return state.historyMask == 0xFF and historyRepeats(state);
}

/// orig: vs_battle_aiUpdatePerceivingActor (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c)
/// Runs one frame of the update for every battle actor: its state, movement history, relations and alert.
pub fn vs_battle_aiUpdatePerceivingActor() callconv(.c) void {
    v.g.D_800F58E0().* = 0;
    const bc = v.g.vs_battle_aiRoomAiState().*.?;
    bc.unk24 = 0;
    if (@as(c_int, bc.unk18) < 0xFFFE) bc.unk18 +%= 1;

    var actor: ?*v.vs_battle_actor = @ptrCast(v.g.vs_battle_actors()[0]);
    while (actor) |a| {
        const flags: c_int = a.unk8;
        a.unkB &= 0xfc;
        const id: c_int = a.id;
        // The state pointer is the scratchpad slot 0x1F80037C + 4 * id, read as a 4-byte guest address.
        const slot: u32 = pad.guest(0x37C) +% @as(u32, @bitCast(@as(c_int, 4) *% id));
        const stateAddr: u32 = @as(*const u32, @ptrFromInt(@as(usize, slot))).*;
        const state: *v.vs_battle_aiState = @ptrFromInt(@as(usize, stateAddr));
        state.unk1D = 0;
        const object: ?*v.actorObject = psx.get(state.object.actorObject);
        state.unk90 = psx.get(a.stats).?.unk954;

        var arg: c_int = 0;
        const outcome: Outcome = outcome_blk: {
            if ((flags & 0x20) != 0) {
                c.vs_battle_aiDisengageActor(@ptrCast(state));
                c.vs_battle_aiResetActorAwarenessIndicator(@ptrCast(state));
                c.vs_battle_aiUnlinkEngagedActor(@ptrCast(state));
                break :outcome_blk .skip;
            }
            c.vs_battle_aiUpdateTemporaryAwarenessIndicator(@ptrCast(state));
            c.vs_battle_aiTickActorDecisionTimers(@ptrCast(state));
            if ((flags & 0x80) != 0) break :outcome_blk .skip;
            c.vs_battle_aiUpdateEngagementBuildup(@ptrCast(state));
            if ((flags & 0x40) != 0 or state.route != null or
                (@as(c_int, psx.getBits(&object.?.flags.bits, "unk16")) |
                @as(c_int, psx.getBits(&object.?.flags.bits, "unk19")) |
                @as(c_int, psx.getBits(psx.get(state.object.D_800F4538_t).?, "unk5AC_15"))) != 0 or
                id == 0)
                break :outcome_blk .skip;
            if (psx.getBits(&object.?.flags.bits, "unk31") != 0) {
                state.unk1D = 1;
                c.vs_battle_aiUpdateTargetPerception(@ptrCast(state), 0);
                break :outcome_blk .skip;
            }

            arg = c.vs_core_elapsedFrameCounterTicks();
            if (v.g.D_800F16F4().* < arg) break :outcome_blk .idle;
            @as(*c_int, @ptrFromInt(pad.addr(0x3FC))).* = arg;
            if (state.unk18 != 0) {
                if (state.unk12A != 0 and state.unk138 == 6) break :outcome_blk .idle;
                state.unk18 = 0;
            }
            if (state.waitTimer != 0) {
                state.waitTimer -%= 1;
                c.vs_battle_aiRecoverMovement(@ptrCast(state));
                c.vs_battle_aiUpdateTargetPerception(@ptrCast(state), 0);
                state.unk1D = 1;
                if (state.unk46 >= 2) state.unk46 -%= 1;
                break :outcome_blk .idle;
            }

            state.unk14 = 0;
            if ((psx.get(state.tilePointer.actorUpdatePoint).?.position & 0xFF00FF) == (state.tile.raw & 0xFF00FF)) {
                if (state.unk46 != 0) {
                    state.unk46 -%= 1;
                    if (state.unk46 == 0) {
                        state.previous.aiWord.value = psx.conv(i32, psx.get(state.tilePointer.actorUpdatePoint).?.position);
                        state.unkFB = 16;
                        state.unk1C = 1;
                        state.unk45 = 40;
                    }
                }
                if (state.unk45 != 0) {
                    state.unk45 -%= 1;
                    if (state.unk45 == 0) {
                        state.detourStep +%= 1;
                        if (@as(c_int, state.detourStep) >= 5) state.detourStep = 1;
                        state.unk45 = 40;
                    }
                }
                state.flags8.b[0] = 1;
            } else {
                state.unk46 = 0xA8C;
                state.flags8.b[0] = 0;
                if (state.unk1C != 0) {
                    c.vs_battle_aiForgetAiActionTarget(@ptrCast(state));
                    state.unk1C = 0;
                }
            }

            if (state.unk124 != 8 and a.unk9 != 8 and state.unk124 != a.unk9) {
                c.func_800E48A8(@ptrCast(state), 1, 64);
            }
            state.unk124 = a.unk9;
            state.tile.raw = psx.get(state.object.actorObject).?.tile.raw;
            const x: c_uint = psx.get(state.position60.actorUpdatePoint).?.position;
            const z: c_uint = psx.get(state.position60.actorUpdatePoint).?.unk4;
            state.position.word[0] = psx.conv(i32, x);
            state.position.word[1] = psx.conv(i32, z);
            state.unk120 = (((z & 0xFFFF) >> 6) << 16) | ((x & 0xFFFF) >> 6);

            // The shift amount is the low 5 bits, as the x86 variable shift masks it.
            if (((@as(c_int, 0x12600) >> @as(u5, @truncate(state.unk124))) & 1) != 0) {
                if (historyStalled(state)) {
                    state.historyMask = 0;
                    state.waitTimer = 30;
                    break :outcome_blk .idle;
                }
                // historyIndex is not checked against the array size: an out-of-range index writes past it, as the C does.
                psx.at(&state.historyX.b, state.historyIndex).* = psx.conv(u8, state.position.word[0]);
                psx.at(&state.historyZ.b, state.historyIndex).* = psx.conv(u8, state.position.word[1]);
                const bit: c_uint = @as(c_uint, 1) << @as(u5, @truncate(state.historyIndex));
                state.historyMask = psx.conv(u8, @as(c_uint, state.historyMask) | bit);
                state.historyIndex = psx.conv(u8, (@as(c_int, state.historyIndex) +% 1) & 7);
            } else {
                state.historyMask = 0;
            }

            if (psx.getBits(&state.movementFlags.update, "unk190_5") > 0) {
                psx.setBits(&state.movementFlags.update, "unk190_5", psx.getBits(&state.movementFlags.update, "unk190_5") -% 1);
            }

            arg = 0;
            var i: c_int = 0;
            while (i < 2) : (i += 1) {
                c.vs_battle_aiUpdateTargetPerception(@ptrCast(state), arg);
                arg = v.g.D_800F58D4().*;
                if (arg == 0) break;
            }

            c.vs_battle_aiRefreshAiResourceRestrictions(state);
            if (c.vs_battle_aiRefreshActionCandidateReadiness(@ptrCast(state)) != 0) break :outcome_blk .idle;

            var alert: c_int = 0;
            const stats: *v.actorUpdateStats = @ptrCast(psx.get(psx.get(state.actor).?.stats).?);
            const current: c_int = psx.getBits(&stats.hp, "current");
            const max: c_int = psx.getBits(&stats.hp, "max");
            if (state.unk2F != 0) {
                if (state.unk2F == 1) {
                    alert = @intFromBool(current *% 4 < max);
                } else if (state.unk2F == 2) {
                    if (current *% 2 < max) alert = 1;
                } else if (state.unk114 != 0) {
                    alert = 1;
                } else {
                    var other: ?*v.vs_battle_actor = v.g.vs_battle_actors()[0];
                    while (other) |o| {
                        const target: c_uint = psx.at(&state.tbl.t.targets.bits32, o.id).*;
                        if (((target >> 5) & 1) != 0 and ((target >> 6) & 3) == 3) {
                            alert = 1;
                            break;
                        }
                        other = psx.get(o.next);
                    }
                }
                if (state.status == 2) alert = 1;
            }
            state.flagE = psx.conv(u8, alert);
            if (state.timer156 != 0) state.timer156 -%= 1;

            if (state.unk1C == 0 and state.previous.aiWord.value >= 0) {
                const targetId: c_int = state.previous.aiWord.b[3];
                const entry: ?*v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), targetId).*;
                if (state.unk40 == 0 or (dec_blk: {
                    state.unk40 -%= 1;
                    break :dec_blk state.unk40 == 0;
                }) or entry == null or
                    (@as(*v.actorObject, @ptrCast(entry.?)).tile.raw & 0xFF00FF) != (state.unk3C & 0xFF00FF))
                {
                    c.vs_battle_aiForgetAiActionTarget(@ptrCast(state));
                }
            }
            break :outcome_blk .done;
        };

        switch (outcome) {
            .done => {},
            .idle => {
                c.func_800E48F8(@ptrCast(state));
                state.unk14 = 1;
            },
            .skip => {
                state.unk14 = 1;
            },
        }

        actor = psx.get(a.next);
    }
}
