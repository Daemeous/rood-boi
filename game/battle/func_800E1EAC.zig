//! An idle actor's wander step: rests and walks in timed periods, picks a
//! free heading or a destination tile, and turns back at the map's edges
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

const Check = *const fn (?*anyopaque, c_int) callconv(.c) c_int;

/// orig: wanderRandomHeading (src/BATTLE/BATTLE.PRG/771A8.c)
/// A heading within half an octant of `direction`'s.
fn wanderRandomHeading(direction: c_int) c_int {
    var offset: c_int = c.func_800E45D4(512);
    const angle: c_int = direction << 9;
    offset +%= 0xFF00;
    return angle +% offset;
}

/// orig: wanderStartWalk (src/BATTLE/BATTLE.PRG/771A8.c)
/// Starts a new walk: no heading yet, and the recovery step.
fn wanderStartWalk(s: *v.wanderState) void {
    s.angle = -1;
    s.count = 0;
    s.cooldown = 60;
    s.mode +%= 1;
    c.func_800E2CCC(@ptrCast(s));
}

/// `1 << rate` as the x86 object shifts (the amount masked to 5 bits).
fn chanceOf(rate: c_int) c_int {
    return @as(c_int, 1) << @truncate(@as(c_uint, @bitCast(rate)));
}

/// orig: func_800E1EAC (src/BATTLE/BATTLE.PRG/771A8.c)
/// One wander step of `state` around `tile` within `range` (63 and up: free roaming by heading).
pub fn func_800E1EAC(state: ?*v.wanderState, tile: c_uint, range: c_uint) callconv(.c) void {
    const s = state.?;
    s.timer +%= 1;
    const rate: c_int = s.rate;
    const rest: u16 = s.restPeriod;
    const move: u16 = s.movePeriod;
    const check: Check = if (psx.get(s.object).?.flags & 0x200000 != 0)
        @ptrCast(&c.func_800E0678)
    else
        @ptrCast(&c.D_800D9B6C);
    if (s.mode12D == 1) {
        if (s.timer < 31) return;
        if (@as(c_uint, @bitCast(c.func_800E45D4(128))) < 44) {
            s.mode12D = 2;
        } else {
            s.timer = 0;
            return;
        }
    }
    if (s.timer == move) {
        if (@as(c_uint, @bitCast(c.func_800E45D4(128))) < 44) s.timer = 0;
    } else if (@as(c_int, s.timer) >= @as(c_int, rest) +% move) {
        s.timer = if (@as(c_uint, @bitCast(c.func_800E45D4(128))) < 44) move else 0;
    }
    s.flag2 = 1;
    @as(*u8, @ptrFromInt(0x1F8003D0)).* = 0;
    recover: {
        if (!(s.timer < move)) break :recover;
        if (range >= 63) {
            if (s.mode == 0 or c.func_800E45D4(chanceOf(rate)) == 0 or s.cooldown == 0) {
                const value: c_uint = @as(c_uint, @bitCast(c.func_800E45D4(4096))) % range +% 128;
                s.mode = 1;
                s.cooldown = 60;
                s.rangeSquared = @bitCast(value *% value);
                const heading: c_int = c.func_800E45D4(4) *% 2;
                var i: c_int = 0;
                while (i < 4) : (i += 1) {
                    const direction: c_int = (heading +% i *% 2) & 7;
                    if (check(s, direction) != 0) {
                        s.angle = psx.conv(i16, wanderRandomHeading(direction));
                        break;
                    }
                }
                if (i == 4) s.angle = psx.conv(i16, c.func_800E45D4(4096));
            }
            if (s.flag8 == 0) {
                s.cooldown = 60;
            } else if (s.cooldown != 0) {
                s.cooldown -%= 1;
            }
            if (s.flags & 8 != 0) {
                c.func_800E1388(@ptrCast(s), s.tile.raw, s.angle, s.moveArg);
            }
            const steps: [*]v.D_800F16EC_t = @ptrFromInt(0x1F8003EC);
            var boundsHeading: c_int = @as(c_int, s.angle) >> 9;
            var boundsIndex: c_int = 0;
            while (boundsIndex < 2) : ({
                boundsHeading = (boundsHeading +% 2) & 7;
                boundsIndex += 1;
            }) {
                const step = psx.at(steps, boundsHeading);
                const x: c_int = @as(c_int, psx.getBits(&s.tile.p, "x")) +% psx.getBits(step, "dx");
                const z: c_int = @as(c_int, psx.getBits(&s.tile.p, "z")) +% psx.getBits(step, "dz");
                const bounds = v.g.D_800F58BC().*.?;
                if (@as(c_int, bounds.maxZ) < z or z < bounds.minZ or @as(c_int, bounds.maxX) < x or x < bounds.minX or
                    (psx.at(psx.at(gridRows(), z), x).* & 0x800) != 0)
                {
                    s.cooldown = 0;
                    break;
                }
            }
            c.func_800E50A0(s, 1, s.angle);
            return;
        }
        s.flag3 = 1;
        if (c.func_800E45D4(chanceOf(rate)) == 0) s.mode = 0;
        switch (s.mode) {
            0 => {
                wanderStartWalk(s);
                return;
            },
            1 => {
                const result = c.func_800E1BB8(@ptrCast(s), tile, range, 3);
                if (result < 0) {
                    s.mode = 0;
                } else if (result > 0) {
                    s.mode +%= 1;
                    s.dest = s.goal;
                }
            },
            2 => {
                s.dest = s.goal;
                if (s.flags & 8 != 0) {
                    const heading: c_int = c.ratan2(
                        @as(c_int, s.x) -% @as(c_int, psx.getBits(&s.dest.p, "x")) *% 128 -% 64,
                        @as(c_int, s.z) -% @as(c_int, psx.getBits(&s.dest.p, "z")) *% 128 -% 64,
                    ) & 4095;
                    s.saved = s.dest;
                    c.func_800E1388(@ptrCast(s), s.tile.raw, heading, s.moveArg);
                }
                c.func_800E50A0(s, 0, 0);
                if (s.flag8 == 0) {
                    s.cooldown = 60;
                } else if (s.cooldown != 0) {
                    s.cooldown -%= 1;
                }
                if ((s.dest.raw & 0xFF00FF) == (s.tile.raw & 0xFF00FF) or s.cooldown == 0) s.mode = 0;
                return;
            },
            else => s.mode = 0,
        }
    }
    c.func_800E2CCC(@ptrCast(s));
}

/// The collision grid: the guest pointer in scratchpad word 0x1F8003C0, rows of 32 tiles.
fn gridRows() [*][32]u16 {
    const word: *const u32 = @ptrFromInt(0x1F8003C0);
    return @ptrFromInt(@as(usize, word.*));
}
