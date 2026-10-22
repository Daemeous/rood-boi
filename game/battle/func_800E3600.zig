//! Approach steering for a movement state: takes the path, the next tile, or
//! the recovery route (src/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

const Test = *const fn (?*anyopaque, c_int) callconv(.c) c_int;
const Table4 = *const fn (?*anyopaque, c_int, c_int, Test) callconv(.c) c_int;
const Table3 = *const fn (?*anyopaque, c_int, Test) callconv(.c) c_int;

/// orig: func_800E3600 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Steers an approaching movement state toward the tile on its angle, or onto its path, recovery or next tile.
pub fn func_800E3600(approach: ?*v.func_800E0850_t, angleArg: c_int, mode: c_int) callconv(.c) void {
    const state: *v.movementApproachState = @ptrCast(@alignCast(approach.?));
    const angle: c_uint = psx.conv(c_uint, angleArg);
    const approachTest: Test = if (mode == 2 or mode == 3) @ptrCast(&c.func_800E0678) else @ptrCast(&c.D_800D9B6C);
    const mask: c_int = if (mode == 2 or mode == 3) 12 else 11;
    const table = v.g.D_800F1760();

    if ((state.path & 1) != 0) {
        const action = c.func_800E2F5C(@ptrCast(state), @ptrCast(approachTest), mask);
        if (action < 0) {
            c.func_800E4BB8(@ptrCast(state));
            c.func_800E2CCC(@ptrCast(state));
            return;
        }
        state.heading = psx.conv(i16, (state.path >> 1) & 7);
        c.func_800DEEFC(@ptrCast(state), action);
        return;
    }

    var quadrant: c_int = psx.conv(c_int, (angle & 0xFFF) >> 10);
    var nearest: c_int = quadrant *% 2;
    if ((angle & 0x3FF) > 512) {
        nearest = ((quadrant +% 1) & 3) *% 2;
    }
    var point: v.vs_battle_movementPosition = state.tile;
    const dxTable: [*]align(1) v.D_800F16EC_t = @ptrFromInt(0x1F8003EC);
    point._bytes[0] = psx.conv(u8, @as(c_int, point._bytes[0]) +% @as(c_int, psx.getBits(psx.at(dxTable, nearest), "dx")));
    point._bytes[2] = psx.conv(u8, @as(c_int, point._bytes[2]) +% @as(c_int, psx.getBits(psx.at(dxTable, nearest), "dz")));

    var target: c_int = undefined;
    if ((nearest & 2) != 0) {
        target = @as(c_int, psx.getBits(&state.destination.p, "x"));
        quadrant = @as(c_int, psx.getBits(&state.tile.p, "x"));
    } else {
        target = @as(c_int, psx.getBits(&state.destination.p, "z"));
        quadrant = @as(c_int, psx.getBits(&state.tile.p, "z"));
    }
    var distance: c_int = @as(c_int, state.radius);
    target -%= quadrant;
    quadrant = if (target >= 0) target else 0 -% target;
    if (distance >= quadrant) {
        distance = quadrant;
    }
    point.raw = c.func_800E49D8(@ptrCast(state), point.raw, nearest, distance);
    if (point.raw != 0) {
        point._bytes[1] = 0;
        state.next = point;
        c.func_800DEEFC(@ptrCast(state), 14);
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
                if (psx.conv(c_uint, @as(c_int, state.timer) -% 1) >= 126) {
                    const first: c_uint = c.func_800D8400(@ptrCast(state), left, right, angle);
                    quadrant = psx.conv(c_int, first >> 24);
                    if (quadrant == 2) {
                        if (state.timer == 0) state.timer = 254;
                        c.func_800E2CCC(@ptrCast(state));
                        return;
                    }
                    if (first != 0) {
                        if (c.func_800E47E4(@ptrCast(state), first) != 0 and
                            c.func_800E4180(@ptrCast(state), 128, 2, @as(c_int, state.unk89)) != 0) return;
                        state.next.raw = first;
                        c.func_800DEEFC(@ptrCast(state), 14);
                        return;
                    }
                }
                const f0: Table4 = @ptrFromInt(@intFromPtr((psx.at(&table[0], mode).*).?));
                if (f0(state, left, right, approachTest) == 0) {
                    c.func_800E2CCC(@ptrCast(state));
                    return;
                }
                state.heading = psx.conv(i16, (state.path >> 1) & 7);
                c.func_800DEEFC(@ptrCast(state), 5);
                return;
            }
        } else {
            if (rightOpen != 0) {
                if (approachTest(state, diagonal) == 0) {
                    state.heading = if (nearest == left) psx.conv(i16, left) else psx.conv(i16, right);
                    c.func_800DEEFC(@ptrCast(state), 5);
                    return;
                }
                state.heading = psx.conv(i16, angle);
                c.func_800DEEFC(@ptrCast(state), 7);
                return;
            }
        }
        state.heading = if (rightOpen == 0) psx.conv(i16, left) else psx.conv(i16, right);

        if (psx.conv(c_uint, @as(c_int, state.timer) -% 1) >= 126) {
            const second: c_uint = c.func_800D8400(@ptrCast(state), 0, 0, psx.conv(c_uint, if (leftOpen == 0) left else right));
            quadrant = psx.conv(c_int, second >> 24);
            if (quadrant == 2) {
                if (state.timer == 0) state.timer = 254;
                c.func_800E2CCC(@ptrCast(state));
                return;
            }
            if (second != 0) {
                if (c.func_800E47E4(@ptrCast(state), second) != 0 and
                    c.func_800E4180(@ptrCast(state), 128, 2, @as(c_int, state.unk89)) != 0) return;
                state.next.raw = second;
                c.func_800DEEFC(@ptrCast(state), 14);
                return;
            }
        }

        var action: c_int = 0;
        if (mode == 0 or mode == 2) {
            if ((@as(c_int, state.heading) & 2) != 0) {
                if (psx.getBits(&state.tile.p, "x") == psx.getBits(&state.destination.p, "x")) action = 1;
            } else {
                if (psx.getBits(&state.tile.p, "z") == psx.getBits(&state.destination.p, "z")) action = 1;
            }
        } else {
            action = 0;
            if (state.unk4 != 0) {
                const sel: c_int = if (leftOpen != 0) right else left;
                const angleDelta: c_uint = (psx.conv(c_uint, sel << 9) -% angle) & 0xFFF;
                action = @as(c_int, @intFromBool(c.rcos(psx.conv(c_int, angleDelta)) >= 3959));
            }
        }
        if (action != 0) {
            const f1: Table4 = @ptrFromInt(@intFromPtr((psx.at(&table[1], mode).*).?));
            if (f1(state, @as(c_int, state.heading), if (leftOpen != 0) 1 else -1, approachTest) == 0) {
                c.func_800E2CCC(@ptrCast(state));
                return;
            }
            state.heading = psx.conv(i16, (state.path >> 1) & 7);
            c.func_800DEEFC(@ptrCast(state), 5);
            return;
        }
        c.func_800DEEFC(@ptrCast(state), 5);
        return;
    }

    distance = psx.conv(c_int, (angle >> 9) & 6);
    if (approachTest(state, distance) != 0) {
        state.heading = psx.conv(i16, angle);
        c.func_800DEEFC(@ptrCast(state), 7);
        return;
    }
    if (psx.conv(c_uint, @as(c_int, state.timer) -% 1) >= 126) {
        const third: c_uint = c.func_800D8400(@ptrCast(state), 0, 0, psx.conv(c_uint, distance));
        quadrant = psx.conv(c_int, third >> 24);
        if (quadrant == 2) {
            if (state.timer == 0) state.timer = 254;
            c.func_800E2CCC(@ptrCast(state));
            return;
        }
        if (third != 0) {
            if (c.func_800E47E4(@ptrCast(state), third) != 0 and
                c.func_800E4180(@ptrCast(state), 128, 2, @as(c_int, state.unk89)) != 0) return;
            state.next.raw = third;
            c.func_800DEEFC(@ptrCast(state), 14);
            return;
        }
    }
    if (mode == 1 and state.unk4 == 0) {
        c.func_800E2CCC(@ptrCast(state));
        return;
    }
    const f2: Table3 = @ptrFromInt(@intFromPtr((psx.at(&table[2], mode).*).?));
    if (f2(state, distance, approachTest) == 0) {
        c.func_800E2CCC(@ptrCast(state));
        return;
    }
    state.heading = psx.conv(i16, (state.path >> 1) & 7);
    c.func_800DEEFC(@ptrCast(state), 5);
}
