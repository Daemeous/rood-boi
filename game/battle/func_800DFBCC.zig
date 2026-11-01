//! Follows a route edge of the area graph from the actor's tile: a gate,
//! a jump, a step along the edge, or a new destination
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

const Position = v.vs_battle_movementPosition;

/// A scratchpad word holding a guest pointer.
fn slotPtr(comptime T: type, address: usize) T {
    return @ptrFromInt(@as(usize, @as(*const u32, @ptrFromInt(address)).*));
}

/// `((D_800F16EC_t*)0x1F8003EC)[direction]`: a heading's tile step.
fn step(direction: c_int) *v.D_800F16EC_t {
    return psx.at(@as([*]v.D_800F16EC_t, @ptrFromInt(0x1F8003EC)), direction);
}

/// orig: routeWord (src/BATTLE/BATTLE.PRG/771A8.c)
fn routeWord(pair: *const [2]u16) c_uint {
    return @as(*align(2) const c_uint, @ptrCast(pair)).*;
}

/// orig: routeHalf (src/BATTLE/BATTLE.PRG/771A8.c)
fn routeHalf(pair: *const [2]u16, index: usize) u16 {
    return pair[index];
}

/// orig: routeTile (src/BATTLE/BATTLE.PRG/771A8.c)
/// The area grid's tile (scratchpad 0x1F8003C0, rows of 32).
fn routeTile(z: c_int, x: c_int) c_uint {
    return psx.at(psx.at(slotPtr([*][32]u16, 0x1F8003C0), z), x).*;
}

/// orig: routeSetArea (src/BATTLE/BATTLE.PRG/771A8.c)
fn routeSetArea(s: *v.routeEdgeState, position: Position) c_int {
    s.area = psx.conv(u8, routeTile(psx.getBits(&position.p, "z"), psx.getBits(&position.p, "x")) & 15);
    return 6;
}

/// The wall grid's tile (scratchpad 0x1F8003BC, rows of 32).
fn wallTile(z: c_int, x: c_int) u16 {
    return psx.at(psx.at(slotPtr([*][32]u16, 0x1F8003BC), z), x).*;
}

/// The C's `center:` tail: head for the target actor, or the candidate tile's centre.
fn center(s: *v.routeEdgeState, candidate: Position) c_int {
    const target = s.target;
    if (target != 16 and psx.at(@as([*]const u32, @ptrFromInt(0x1F80037C)), target).* != 0) {
        const actor = slotPtr(*v.routeEdgeActor, 0x1F80037C + 4 * @as(usize, target));
        s.moveX = psx.at(psx.get(actor.position).?, 0).*;
        const again = slotPtr(*v.routeEdgeActor, 0x1F80037C + 4 * @as(usize, s.target));
        s.moveZ = psx.at(psx.get(again.position).?, 2).*;
    } else {
        s.moveX = @as(c_int, psx.getBits(&candidate.p, "x")) *% 128 +% 64;
        s.moveZ = @as(c_int, psx.getBits(&candidate.p, "z")) *% 128 +% 64;
    }
    s.angle = psx.conv(i16, c.ratan2(@as(c_int, s.x) -% s.moveX, @as(c_int, s.z) -% s.moveZ) & 0xFFF);
    s.cooldown = s.period;
    return 4;
}

/// The C's `stop:` / `accept:` tails: the next tile (on the ground for a stop).
fn accept(s: *v.routeEdgeState, candidate: Position) c_int {
    s.next = candidate;
    return 14;
}

fn stop(s: *v.routeEdgeState, candidate: *Position) c_int {
    psx.setBits(&candidate.p, "y", 0);
    return accept(s, candidate.*);
}

/// The C's `first:` / `second:` tail: head for that route's far tile, or to the centre if already there.
fn headFor(s: *v.routeEdgeState, raw: c_uint, candidate: Position) c_int {
    s.destination.raw = raw;
    if ((s.destination.raw & 0xFF00FF) == (s.tile.raw & 0xFF00FF)) return center(s, candidate);
    return 6;
}

fn addStep(candidate: *Position, amount: c_int, direction: c_int) void {
    psx.setBits(&candidate.p, "x", psx.conv(u8, @as(c_int, psx.getBits(&candidate.p, "x")) +% amount *% psx.getBits(step(direction), "dx")));
    psx.setBits(&candidate.p, "z", psx.conv(u8, @as(c_int, psx.getBits(&candidate.p, "z")) +% amount *% psx.getBits(step(direction), "dz")));
}

/// orig: func_800DFBCC (src/BATTLE/BATTLE.PRG/771A8.c)
/// Takes route edge `index` of `area` from the actor's tile: 14 a next tile, 5 a heading, 4 heading for a point, 6 a new destination or area.
pub fn func_800DFBCC(state: ?*v.routeEdgeState, index: c_int, area: c_int) callconv(.c) c_int {
    const s = state.?;
    const position: Position = .{ .raw = @as(*align(4) const c_uint, @ptrCast(&s.tile)).* };
    var routes: [2]u16 align(4) = undefined;
    @as(*c_uint, @ptrCast(&routes)).* = psx.at(slotPtr([*]const c_uint, 0x1F8003F4), area *% 16 +% index).*;
    if (routeWord(&routes) & 0x1BE0 == 0) {
        return 6;
    }
    s.area = psx.conv(u8, area);
    var count: c_int = 1;
    if (routeWord(&routes) & 0x1BE00000 != 0) {
        count = 2;
    }
    // Assigned before every path that reads it (the tails reach `center`
    // only for a route matching the tile, which assigned it).
    var candidate: Position = .{ .raw = 0 };

    var i: c_int = 0;
    while (i < count) : (i += 1) {
        const route: c_uint = if (i == 0) routeHalf(&routes, 0) else routeHalf(&routes, 1);
        if (((route >> 5) & 31) != psx.getBits(&position.p, "x") or (route & 31) != psx.getBits(&position.p, "z")) {
            continue;
        }
        candidate = position;
        if (route & 0x400 != 0) {
            if (route & 0x8000 != 0) {
                continue;
            }
            const result = c.func_800DFAF8(@bitCast(psx.addr(s)), position.raw, 1);
            if (result >= 0) {
                i = result >> 1;
                if (c.func_8008E320(i) >= 0) {
                    psx.setBits(&candidate.p, "y", psx.conv(u8, i +% 2));
                    const gates: *v.routeEdgeGlobals = @ptrCast(v.g.D_800F58BC().*.?);
                    // The shift amount as x86 masks it (the C's is undefined past 31).
                    if ((@as(c_int, gates.closedGates) >> @truncate(@as(c_uint, psx.getBits(&candidate.p, "y")))) & 1 == 0) {
                        s.gate = psx.getBits(&candidate.p, "y");
                        if (s.id != 0) {
                            c.func_800E4BE0(psx.getBits(&candidate.p, "y"));
                        }
                        c.func_800DF9A8(@ptrCast(&candidate), @ptrCast(&s.exit));
                        return accept(s, candidate);
                    }
                }
            }
            psx.setBits(&candidate.p, "x", @truncate((route >> 5) & 31));
            psx.setBits(&candidate.p, "z", @truncate(route & 31));
            return center(s, candidate);
        }
        if (route & 0x6000 != 0) {
            s.area = psx.conv(u8, routeTile(psx.getBits(&candidate.p, "z"), psx.getBits(&candidate.p, "x")) & 15);
            s.unk5 = 0;
            const amount: c_int = @as(c_int, @intCast((route >> 13) & 3)) + 1;
            const stepIndex = psx.at(v.g.D_800F175C(), (route >> 11) & 3);
            addStep(&candidate, amount, stepIndex.*);
            const result = c.func_800D954C(@ptrCast(s), candidate.raw);
            if (result < 0) {
                return center(s, candidate);
            }
            if (result <= 0) {
                continue;
            }
            return stop(s, &candidate);
        }
        const direction = psx.at(v.g.D_800F175C(), (route >> 11) & 3);
        addStep(&candidate, 1, direction.*);
        if (c.func_800D954C(@ptrCast(s), candidate.raw) <= 0) {
            return center(s, candidate);
        }
        if (wallTile(psx.getBits(&candidate.p, "z"), psx.getBits(&candidate.p, "x")) & 0x1C00 != 0) {
            return stop(s, &candidate);
        }
        const passages = slotPtr([*]const u8, 0x1F8003C4);
        const width: c_int = @as(*const u8, @ptrFromInt(0x1F8003DC)).*;
        const cell = psx.at(passages, @as(c_int, psx.getBits(&position.p, "z")) *% width +% psx.getBits(&position.p, "x")).*;
        // *direction indexes the 8 headings: below 8 in the game's table; the shift as x86 masks it.
        if ((@as(c_int, cell) >> @truncate(@as(c_uint, direction.*))) & 1 != 0) {
            return stop(s, &candidate);
        }
        s.heading = direction.*;
        return 5;
    }

    const word = routeWord(&routes);
    const firstRaw = ((@as(c_uint, routeHalf(&routes, 0)) & 31) << 16) | ((word >> 5) & 31);
    const secondRaw = (word & 0x1F0000) | ((word >> 21) & 31);
    if ((word & 0x8400) == 0x8400) {
        if (count == 1) return routeSetArea(s, position);
        if ((word & 0x84000000) == 0x84000000) return routeSetArea(s, position);
        return headFor(s, secondRaw, candidate);
    }
    if ((word & 0x84000000) == 0x84000000) {
        return headFor(s, firstRaw, candidate);
    }

    i = 0;
    while (i < count) : (i += 1) {
        var route: c_uint = if (i == 0) routeHalf(&routes, 0) else routeHalf(&routes, 1);
        if (wallTile(@intCast(route & 31), @intCast((route >> 5) & 31)) & 0x1C00 == 0) {
            continue;
        }
        route = if (i != 0) routeHalf(&routes, 0) else routeHalf(&routes, 1);
        if (wallTile(@intCast(route & 31), @intCast((route >> 5) & 31)) & 0x1C00 != 0 or count == 1) {
            s.area = psx.conv(u8, routeTile(psx.getBits(&position.p, "z"), psx.getBits(&position.p, "x")) % 16);
            return 6;
        }
        s.destination.raw = ((route & 31) << 16) | ((route >> 5) & 31);
        return 6;
    }

    var x: c_int = psx.getBits(&position.p, "x");
    if (count == 1) {
        return headFor(s, firstRaw, candidate);
    }
    var z: c_int = psx.getBits(&position.p, "z");
    // The C mixes int and u_int here: unsigned arithmetic, the same low 32 bits.
    const dx0: c_uint = @as(c_uint, @bitCast(x)) -% ((word >> 5) & 31);
    const dz0: c_int = z -% (@as(c_int, routeHalf(&routes, 0)) & 31);
    const distance0: c_int = @bitCast(dx0 *% dx0 +% @as(c_uint, @bitCast(dz0 *% dz0)));
    x = @bitCast(@as(c_uint, @bitCast(x)) -% ((word >> 21) & 31));
    z = @bitCast(@as(c_uint, @bitCast(z)) -% ((word >> 16) & 31));
    const distance1: c_int = x *% x +% z *% z;
    if (distance0 < distance1) {
        return headFor(s, firstRaw, candidate);
    }
    return headFor(s, secondRaw, candidate);
}
