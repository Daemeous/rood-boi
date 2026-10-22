//! Per-frame battle actor update: timers, movement history, relations, the
//! action candidate and the alert level of each actor (src/BATTLE/BATTLE.PRG/6FC00.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6FC00");
const c = @import("c_6FC00");

/// How one actor's update ends: falling through, `goto skip` or `goto idle`.
const Outcome = enum { done, idle, skip };

/// orig: historyRepeats (src/BATTLE/BATTLE.PRG/6FC00.c)
/// True when the last two stored history samples are equal in X and Z.
inline fn historyRepeats(state: *v.actorUpdateState) bool {
    return state.historyX.w[0] == state.historyX.w[1] and
        state.historyZ.w[0] == state.historyZ.w[1];
}

/// orig: historyStalled (src/BATTLE/BATTLE.PRG/6FC00.c)
/// True when the history buffer is full and its samples have stopped changing.
inline fn historyStalled(state: *v.actorUpdateState) bool {
    return state.historyMask == 0xFF and historyRepeats(state);
}

/// orig: func_800D8EF4 (src/BATTLE/BATTLE.PRG/6FC00.c)
/// Runs one frame of the update for every battle actor: its state, movement history, relations and alert.
pub fn func_800D8EF4() callconv(.c) void {
    v.g.D_800F58E0().* = 0;
    const bc = v.g.D_800F58BC().*.?;
    bc.unk24 = 0;
    if (@as(c_int, bc.unk18) < 0xFFFE) bc.unk18 +%= 1;

    var actor: ?*v.actorUpdateActor = @ptrCast(v.g.vs_battle_actors()[0]);
    while (actor) |a| {
        const flags: c_int = a.flags;
        psx.setBits(a, "unkB_0", 0);
        const id: c_int = a.id;
        // The state pointer is the scratchpad slot 0x1F80037C + 4 * id, read as a 4-byte guest address.
        const slot: u32 = 0x1F80037C +% @as(u32, @bitCast(@as(c_int, 4) *% id));
        const stateAddr: u32 = @as(*const u32, @ptrFromInt(@as(usize, slot))).*;
        const state: *v.actorUpdateState = @ptrFromInt(@as(usize, stateAddr));
        state.unk1D = 0;
        const object: ?*v.actorObject = psx.get(state.object);
        state.unk90 = psx.get(a.stats).?.unk954;

        var arg: c_int = 0;
        const outcome: Outcome = outcome_blk: {
            if ((flags & 0x20) != 0) {
                c.func_800D82CC(@ptrCast(state));
                c.func_800D821C(@ptrCast(state));
                c.func_800DBF00(@ptrCast(state));
                break :outcome_blk .skip;
            }
            c.func_800D8280(@ptrCast(state));
            c.func_800E4358(@ptrCast(state));
            if ((flags & 0x80) != 0) break :outcome_blk .skip;
            c.func_800DBFE4(@ptrCast(state));
            if ((flags & 0x40) != 0 or state.unk18C != 0 or
                (@as(c_int, psx.getBits(&object.?.flags.bits, "unk16")) |
                @as(c_int, psx.getBits(&object.?.flags.bits, "unk19")) |
                @as(c_int, psx.getBits(object.?, "unk5AC_15"))) != 0 or
                id == 0)
                break :outcome_blk .skip;
            if (psx.getBits(&object.?.flags.bits, "unk31") != 0) {
                state.unk1D = 1;
                c.func_800D87E8(@ptrCast(state), 0);
                break :outcome_blk .skip;
            }

            arg = c.func_800E45B4();
            if (v.g.D_800F16F4().* < arg) break :outcome_blk .idle;
            @as(*c_int, @ptrFromInt(0x1F8003FC)).* = arg;
            if (state.unk18 != 0) {
                if (state.unk12A != 0 and state.unk138 == 6) break :outcome_blk .idle;
                state.unk18 = 0;
            }
            if (state.unk19A != 0) {
                state.unk19A -%= 1;
                c.func_800E2CCC(@ptrCast(state));
                c.func_800D87E8(@ptrCast(state), 0);
                state.unk1D = 1;
                if (state.unk46 >= 2) state.unk46 -%= 1;
                break :outcome_blk .idle;
            }

            state.unk14 = 0;
            if ((psx.get(state.unk64).?.position & 0xFF00FF) == (state.unk34 & 0xFF00FF)) {
                if (state.unk46 != 0) {
                    state.unk46 -%= 1;
                    if (state.unk46 == 0) {
                        state.unk38.value = psx.conv(i32, psx.get(state.unk64).?.position);
                        state.unkFB = 16;
                        state.unk1C = 1;
                        state.unk45 = 40;
                    }
                }
                if (state.unk45 != 0) {
                    state.unk45 -%= 1;
                    if (state.unk45 == 0) {
                        state.unkFA +%= 1;
                        if (@as(c_int, state.unkFA) >= 5) state.unkFA = 1;
                        state.unk45 = 40;
                    }
                }
                state.unk8 = 1;
            } else {
                state.unk46 = 0xA8C;
                state.unk8 = 0;
                if (state.unk1C != 0) {
                    c.func_800E4CE8(@ptrCast(state));
                    state.unk1C = 0;
                }
            }

            if (state.unk124 != 8 and a.unk9 != 8 and state.unk124 != a.unk9) {
                c.func_800E48A8(@ptrCast(state), 1, 64);
            }
            state.unk124 = a.unk9;
            state.unk34 = psx.get(state.object).?.tile.raw;
            const x: c_uint = psx.get(state.unk60).?.position;
            const z: c_uint = psx.get(state.unk60).?.unk4;
            state.unk4C = psx.conv(i32, x);
            state.unk50 = psx.conv(i32, z);
            state.unk120 = (((z & 0xFFFF) >> 6) << 16) | ((x & 0xFFFF) >> 6);

            // The shift amount is the low 5 bits, as the x86 variable shift masks it.
            if (((@as(c_int, 0x12600) >> @as(u5, @truncate(state.unk124))) & 1) != 0) {
                if (historyStalled(state)) {
                    state.historyMask = 0;
                    state.unk19A = 30;
                    break :outcome_blk .idle;
                }
                // historyIndex is not checked against the array size: an out-of-range index writes past it, as the C does.
                psx.at(&state.historyX.b, state.historyIndex).* = psx.conv(u8, state.unk4C);
                psx.at(&state.historyZ.b, state.historyIndex).* = psx.conv(u8, state.unk50);
                const bit: c_uint = @as(c_uint, 1) << @as(u5, @truncate(state.historyIndex));
                state.historyMask = psx.conv(u8, @as(c_uint, state.historyMask) | bit);
                state.historyIndex = psx.conv(u8, (@as(c_int, state.historyIndex) +% 1) & 7);
            } else {
                state.historyMask = 0;
            }

            if (psx.getBits(state, "unk190_5") > 0) {
                psx.setBits(state, "unk190_5", psx.getBits(state, "unk190_5") -% 1);
            }

            arg = 0;
            var i: c_int = 0;
            while (i < 2) : (i += 1) {
                c.func_800D87E8(@ptrCast(state), arg);
                arg = v.g.D_800F58D4().*;
                if (arg == 0) break;
            }

            c.func_800E4F14(state);
            if (c.func_800D85D8(@ptrCast(state)) != 0) break :outcome_blk .idle;

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
                        const target: c_uint = psx.at(&state.targets, o.id).*;
                        if (((target >> 5) & 1) != 0 and ((target >> 6) & 3) == 3) {
                            alert = 1;
                            break;
                        }
                        other = psx.get(o.next);
                    }
                }
                if (state.unk131 == 2) alert = 1;
            }
            state.unkE = psx.conv(u8, alert);
            if (state.unk156 != 0) state.unk156 -%= 1;

            if (state.unk1C == 0 and state.unk38.value >= 0) {
                const targetId: c_int = state.unk38.b[3];
                const entry: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), targetId).*;
                if (state.unk40 == 0 or (dec_blk: {
                    state.unk40 -%= 1;
                    break :dec_blk state.unk40 == 0;
                }) or entry == null or
                    (@as(*v.actorObject, @ptrCast(entry.?)).tile.raw & 0xFF00FF) != (state.unk3C & 0xFF00FF))
                {
                    c.func_800E4CE8(@ptrCast(state));
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
