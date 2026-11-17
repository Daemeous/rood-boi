//! Approach steering for a movement state: takes the path, the next tile, or
//! the recovery route (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const pad = @import("common").pad;
const v = @import("views_771A8");
const c = @import("c_771A8");

const Test = *const fn (?*anyopaque, c_int) callconv(.c) c_int;
const Table4 = *const fn (?*anyopaque, c_int, c_int, Test) callconv(.c) c_int;
const Table3 = *const fn (?*anyopaque, c_int, Test) callconv(.c) c_int;

/// orig: vs_battle_aiResolveDirectionalMovement (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Steers an approaching movement state toward the tile on its angle, or onto its path, recovery or next tile.
pub fn vs_battle_aiResolveDirectionalMovement(approach: ?*v.vs_battle_aiState, angleArg: c_int, mode: c_int) callconv(.c) void {
    const state: *v.vs_battle_aiState = @ptrCast(@alignCast(approach.?));
    const angle: c_uint = psx.conv(c_uint, angleArg);
    const approachTest: Test = if (mode == 2 or mode == 3) @ptrCast(&c.vs_battle_aiCanFlyInDirection) else @ptrCast(&c.vs_battle_aiCanWalkInDirection);
    const mask: c_int = if (mode == 2 or mode == 3) 12 else 11;
    const table = v.g.D_800F1760();

    if ((state.movementFlags.raw & 1) != 0) {
        const action = c.vs_battle_aiFollowObstacleDetour(@ptrCast(state), @ptrCast(approachTest), mask);
        if (action < 0) {
            c.func_800E4BB8(@ptrCast(state));
            c.vs_battle_aiRecoverMovement(@ptrCast(state));
            return;
        }
        state.heading = psx.conv(i16, (state.movementFlags.raw >> 1) & 7);
        c.vs_battle_aiIssueActorCommand(@ptrCast(state), action);
        return;
    }

    var quadrant: c_int = psx.conv(c_int, (angle & 0xFFF) >> 10);
    var nearest: c_int = quadrant *% 2;
    if ((angle & 0x3FF) > 512) {
        nearest = ((quadrant +% 1) & 3) *% 2;
    }
    var point: v.vs_battle_movementPosition = state.tile.position;
    const dxTable: [*]align(1) v.D_800F16EC_t = pad.many(v.D_800F16EC_t, 0x3EC);
    point._bytes[0] = psx.conv(u8, @as(c_int, point._bytes[0]) +% @as(c_int, psx.getBits(psx.at(dxTable, nearest), "dx")));
    point._bytes[2] = psx.conv(u8, @as(c_int, point._bytes[2]) +% @as(c_int, psx.getBits(psx.at(dxTable, nearest), "dz")));

    var target: c_int = undefined;
    if ((nearest & 2) != 0) {
        target = @as(c_int, psx.getBits(&state.destination168.movementPosition.p, "x"));
        quadrant = @as(c_int, psx.getBits(&state.tile.position.p, "x"));
    } else {
        target = @as(c_int, psx.getBits(&state.destination168.movementPosition.p, "z"));
        quadrant = @as(c_int, psx.getBits(&state.tile.position.p, "z"));
    }
    var distance: c_int = @as(c_int, state.radiusB2);
    target -%= quadrant;
    quadrant = if (target >= 0) target else 0 -% target;
    if (distance >= quadrant) {
        distance = quadrant;
    }
    point.raw = c.func_800E49D8(@ptrCast(state), point.raw, nearest, distance);
    if (point.raw != 0) {
        point._bytes[1] = 0;
        state.next.movementPosition = point;
        c.vs_battle_aiIssueActorCommand(@ptrCast(state), 14);
        return;
    }

    if ((angle & 0x3FF) != 0) {
        const sector: c_int = psx.conv(c_int, angle >> 10);
        const left: c_int = ((sector +% 1) *% 2) & 7;
        const right: c_int = (sector *% 2) & 7;
        const diagonal: c_int = ((sector *% 2) +% 1) & 7;
        const leftOpen: c_int = approachTest(state, left);
        const rightOpen: c_int = approachTest(state, right);
        if (leftOpen == 0) {
            if (rightOpen == 0) {
                if (psx.conv(c_uint, @as(c_int, state.timer156) -% 1) >= 126) {
                    const first: c_uint = c.vs_battle_aiFindAdjacentMovementDestination(@ptrCast(state), left, right, angle);
                    quadrant = psx.conv(c_int, first >> 24);
                    if (quadrant == 2) {
                        if (state.timer156 == 0) state.timer156 = 254;
                        c.vs_battle_aiRecoverMovement(@ptrCast(state));
                        return;
                    }
                    if (first != 0) {
                        if (c.func_800E47E4(@ptrCast(state), first) != 0 and
                            c.func_800E4180(@ptrCast(state), 128, 2, @as(c_int, state.unk89)) != 0) return;
                        state.next.movementPosition.raw = first;
                        c.vs_battle_aiIssueActorCommand(@ptrCast(state), 14);
                        return;
                    }
                }
                const f0: Table4 = @ptrFromInt(@intFromPtr((psx.at(&table[0], mode).*).?));
                if (f0(state, left, right, approachTest) == 0) {
                    c.vs_battle_aiRecoverMovement(@ptrCast(state));
                    return;
                }
                state.heading = psx.conv(i16, (state.movementFlags.raw >> 1) & 7);
                c.vs_battle_aiIssueActorCommand(@ptrCast(state), 5);
                return;
            }
        } else {
            if (rightOpen != 0) {
                if (approachTest(state, diagonal) == 0) {
                    state.heading = if (nearest == left) psx.conv(i16, left) else psx.conv(i16, right);
                    c.vs_battle_aiIssueActorCommand(@ptrCast(state), 5);
                    return;
                }
                state.heading = psx.conv(i16, angle);
                c.vs_battle_aiIssueActorCommand(@ptrCast(state), 7);
                return;
            }
        }
        state.heading = if (rightOpen == 0) psx.conv(i16, left) else psx.conv(i16, right);

        if (psx.conv(c_uint, @as(c_int, state.timer156) -% 1) >= 126) {
            const second: c_uint = c.vs_battle_aiFindAdjacentMovementDestination(@ptrCast(state), 0, 0, psx.conv(c_uint, if (leftOpen == 0) left else right));
            quadrant = psx.conv(c_int, second >> 24);
            if (quadrant == 2) {
                if (state.timer156 == 0) state.timer156 = 254;
                c.vs_battle_aiRecoverMovement(@ptrCast(state));
                return;
            }
            if (second != 0) {
                if (c.func_800E47E4(@ptrCast(state), second) != 0 and
                    c.func_800E4180(@ptrCast(state), 128, 2, @as(c_int, state.unk89)) != 0) return;
                state.next.movementPosition.raw = second;
                c.vs_battle_aiIssueActorCommand(@ptrCast(state), 14);
                return;
            }
        }

        var action: c_int = 0;
        if (mode == 0 or mode == 2) {
            if ((@as(c_int, state.heading) & 2) != 0) {
                if (psx.getBits(&state.tile.position.p, "x") == psx.getBits(&state.destination168.movementPosition.p, "x")) action = 1;
            } else {
                if (psx.getBits(&state.tile.position.p, "z") == psx.getBits(&state.destination168.movementPosition.p, "z")) action = 1;
            }
        } else {
            action = 0;
            if (state.flags4.b[0] != 0) {
                const sel: c_int = if (leftOpen != 0) right else left;
                const angleDelta: c_uint = (psx.conv(c_uint, sel << 9) -% angle) & 0xFFF;
                action = @as(c_int, @intFromBool(c.rcos(psx.conv(c_int, angleDelta)) >= 3959));
            }
        }
        if (action != 0) {
            const f1: Table4 = @ptrFromInt(@intFromPtr((psx.at(&table[1], mode).*).?));
            if (f1(state, @as(c_int, state.heading), if (leftOpen != 0) 1 else -1, approachTest) == 0) {
                c.vs_battle_aiRecoverMovement(@ptrCast(state));
                return;
            }
            state.heading = psx.conv(i16, (state.movementFlags.raw >> 1) & 7);
            c.vs_battle_aiIssueActorCommand(@ptrCast(state), 5);
            return;
        }
        c.vs_battle_aiIssueActorCommand(@ptrCast(state), 5);
        return;
    }

    distance = psx.conv(c_int, (angle >> 9) & 6);
    if (approachTest(state, distance) != 0) {
        state.heading = psx.conv(i16, angle);
        c.vs_battle_aiIssueActorCommand(@ptrCast(state), 7);
        return;
    }
    if (psx.conv(c_uint, @as(c_int, state.timer156) -% 1) >= 126) {
        const third: c_uint = c.vs_battle_aiFindAdjacentMovementDestination(@ptrCast(state), 0, 0, psx.conv(c_uint, distance));
        quadrant = psx.conv(c_int, third >> 24);
        if (quadrant == 2) {
            if (state.timer156 == 0) state.timer156 = 254;
            c.vs_battle_aiRecoverMovement(@ptrCast(state));
            return;
        }
        if (third != 0) {
            if (c.func_800E47E4(@ptrCast(state), third) != 0 and
                c.func_800E4180(@ptrCast(state), 128, 2, @as(c_int, state.unk89)) != 0) return;
            state.next.movementPosition.raw = third;
            c.vs_battle_aiIssueActorCommand(@ptrCast(state), 14);
            return;
        }
    }
    if (mode == 1 and state.flags4.b[0] == 0) {
        c.vs_battle_aiRecoverMovement(@ptrCast(state));
        return;
    }
    const f2: Table3 = @ptrFromInt(@intFromPtr((psx.at(&table[2], mode).*).?));
    if (f2(state, distance, approachTest) == 0) {
        c.vs_battle_aiRecoverMovement(@ptrCast(state));
        return;
    }
    state.heading = psx.conv(i16, (state.movementFlags.raw >> 1) & 7);
    c.vs_battle_aiIssueActorCommand(@ptrCast(state), 5);
}
