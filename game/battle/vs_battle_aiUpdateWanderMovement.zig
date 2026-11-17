//! An idle actor's wander step: rests and walks in timed periods, picks a
//! free heading or a destination tile, and turns back at the map's edges
//! (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

const Check = *const fn (?*anyopaque, c_int) callconv(.c) c_int;

/// orig: wanderRandomHeading (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// A heading within half an octant of `direction`'s.
fn wanderRandomHeading(direction: c_int) c_int {
    var offset: c_int = c.vs_battle_aiNextActorRandomMasked(512);
    const angle: c_int = direction << 9;
    offset +%= 0xFF00;
    return angle +% offset;
}

/// orig: wanderStartWalk (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Starts a new walk: no heading yet, and the recovery step.
fn wanderStartWalk(s: *v.vs_battle_aiState) void {
    s.angle = -1;
    s.count = 0;
    s.cooldown = 60;
    s.mode +%= 1;
    c.vs_battle_aiRecoverMovement(@ptrCast(s));
}

/// `1 << rate` as the x86 object shifts (the amount masked to 5 bits).
fn chanceOf(rate: c_int) c_int {
    return @as(c_int, 1) << @truncate(@as(c_uint, @bitCast(rate)));
}

/// orig: vs_battle_aiUpdateWanderMovement (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// One wander step of `state` around `tile` within `range` (63 and up: free roaming by heading).
pub fn vs_battle_aiUpdateWanderMovement(state: ?*v.vs_battle_aiState, tile: c_uint, range: c_uint) callconv(.c) void {
    const s = state.?;
    s.timerC2 +%= 1;
    const rate: c_int = s.rate;
    const rest: u16 = s.restPeriod;
    const move: u16 = s.movePeriod;
    const check: Check = if (psx.get(s.object.wanderObject).?.flags & 0x200000 != 0)
        @ptrCast(&c.vs_battle_aiCanFlyInDirection)
    else
        @ptrCast(&c.vs_battle_aiCanWalkInDirection);
    if (s.mode12D == 1) {
        if (s.timerC2 < 31) return;
        if (@as(c_uint, @bitCast(c.vs_battle_aiNextActorRandomMasked(128))) < 44) {
            s.mode12D = 2;
        } else {
            s.timerC2 = 0;
            return;
        }
    }
    if (s.timerC2 == move) {
        if (@as(c_uint, @bitCast(c.vs_battle_aiNextActorRandomMasked(128))) < 44) s.timerC2 = 0;
    } else if (@as(c_int, s.timerC2) >= @as(c_int, rest) +% move) {
        s.timerC2 = if (@as(c_uint, @bitCast(c.vs_battle_aiNextActorRandomMasked(128))) < 44) move else 0;
    }
    s.flags0.b[2] = 1;
    pad.at(u8, 0x3D0).* = 0;
    recover: {
        if (!(s.timerC2 < move)) break :recover;
        if (range >= 63) {
            if (s.mode == 0 or c.vs_battle_aiNextActorRandomMasked(chanceOf(rate)) == 0 or s.cooldown == 0) {
                const value: c_uint = @as(c_uint, @bitCast(c.vs_battle_aiNextActorRandomMasked(4096))) % range +% 128;
                s.mode = 1;
                s.cooldown = 60;
                s.rangeSquaredC4 = @bitCast(value *% value);
                const heading: c_int = c.vs_battle_aiNextActorRandomMasked(4) *% 2;
                var i: c_int = 0;
                while (i < 4) : (i += 1) {
                    const direction: c_int = (heading +% i *% 2) & 7;
                    if (check(s, direction) != 0) {
                        s.angle = psx.conv(i16, wanderRandomHeading(direction));
                        break;
                    }
                }
                if (i == 4) s.angle = psx.conv(i16, c.vs_battle_aiNextActorRandomMasked(4096));
            }
            if (s.flags8.b[0] == 0) {
                s.cooldown = 60;
            } else if (s.cooldown != 0) {
                s.cooldown -%= 1;
            }
            if (s.flags & 8 != 0) {
                c.vs_battle_aiProjectClearRadialDestination(@ptrCast(s), s.tile.position.raw, s.angle, @bitCast(s.distance));
            }
            const steps: [*]v.D_800F16EC_t = pad.many(v.D_800F16EC_t, 0x3EC);
            var boundsHeading: c_int = @as(c_int, s.angle) >> 9;
            var boundsIndex: c_int = 0;
            while (boundsIndex < 2) : ({
                boundsHeading = (boundsHeading +% 2) & 7;
                boundsIndex += 1;
            }) {
                const step = psx.at(steps, boundsHeading);
                const x: c_int = @as(c_int, psx.getBits(&s.tile.position.p, "x")) +% psx.getBits(step, "dx");
                const z: c_int = @as(c_int, psx.getBits(&s.tile.position.p, "z")) +% psx.getBits(step, "dz");
                const bounds = v.g.vs_battle_aiRoomAiState().*.?;
                if (@as(c_int, bounds.maxZ) < z or z < bounds.minZ or @as(c_int, bounds.maxX) < x or x < bounds.minX or
                    (psx.at(psx.at(gridRows(), z), x).* & 0x800) != 0)
                {
                    s.cooldown = 0;
                    break;
                }
            }
            c.vs_battle_aiDispatchAiLocomotionMode(s, 1, s.angle);
            return;
        }
        s.flags0.b[3] = 1;
        if (c.vs_battle_aiNextActorRandomMasked(chanceOf(rate)) == 0) s.mode = 0;
        switch (s.mode) {
            0 => {
                wanderStartWalk(s);
                return;
            },
            1 => {
                const result = c.vs_battle_aiChooseNextWanderTile(@ptrCast(s), tile, range, 3);
                if (result < 0) {
                    s.mode = 0;
                } else if (result > 0) {
                    s.mode +%= 1;
                    s.destination168.movementPosition = s.goal;
                }
            },
            2 => {
                s.destination168.movementPosition = s.goal;
                if (s.flags & 8 != 0) {
                    const heading: c_int = c.ratan2(
                        @as(c_int, s.position.v.vx) -% @as(c_int, psx.getBits(&s.destination168.movementPosition.p, "x")) *% 128 -% 64,
                        @as(c_int, s.position.v.vz) -% @as(c_int, psx.getBits(&s.destination168.movementPosition.p, "z")) *% 128 -% 64,
                    ) & 4095;
                    s.previous48.movementPosition = s.destination168.movementPosition;
                    c.vs_battle_aiProjectClearRadialDestination(@ptrCast(s), s.tile.position.raw, heading, @bitCast(s.distance));
                }
                c.vs_battle_aiDispatchAiLocomotionMode(s, 0, 0);
                if (s.flags8.b[0] == 0) {
                    s.cooldown = 60;
                } else if (s.cooldown != 0) {
                    s.cooldown -%= 1;
                }
                if ((s.destination168.movementPosition.raw & 0xFF00FF) == (s.tile.position.raw & 0xFF00FF) or s.cooldown == 0) s.mode = 0;
                return;
            },
            else => s.mode = 0,
        }
    }
    c.vs_battle_aiRecoverMovement(@ptrCast(s));
}

/// The collision grid: the guest pointer in scratchpad word 0x1F8003C0, rows of 32 tiles.
fn gridRows() [*][32]u16 {
    return pad.nav.cells.?;
}
