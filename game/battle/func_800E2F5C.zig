//! Wall-following step of a blocked path: turns along the obstacle,
//! stops at the map's edges or the path's bounds, and finds a detour tile
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// `((D_800F16EC_t*)0x1F8003EC)[direction]`: a heading's tile step.
fn step(direction: c_int) *v.D_800F16EC_t {
    return psx.at(@as([*]v.D_800F16EC_t, @ptrFromInt(0x1F8003EC)), direction);
}

/// The other actor's path state in scratchpad slot `0x1F80037C + 4 * index`.
fn blockerState(index: c_int) *v.movementPathState {
    const slot = psx.at(@as([*]const u32, @ptrFromInt(0x1F80037C)), index);
    return @ptrFromInt(@as(usize, slot.*));
}

/// The tile one step from `tile` toward `direction` (y 0). The C builds it in an
/// uninitialised local: the object keeps the status byte 0 for `ahead`, and for
/// the detour `point` a caller's register byte; 0 here.
fn neighbour(tile: v.vs_battle_movementPosition, direction: c_int) v.vs_battle_movementPosition {
    var p: v.vs_battle_movementPosition = .{ .raw = 0 };
    psx.setBits(&p.p, "x", psx.conv(u8, @as(c_int, psx.getBits(&tile.p, "x")) +% psx.getBits(step(direction), "dx")));
    psx.setBits(&p.p, "z", psx.conv(u8, @as(c_int, psx.getBits(&tile.p, "z")) +% psx.getBits(step(direction), "dz")));
    psx.setBits(&p.p, "y", 0);
    return p;
}

/// orig: func_800E2F5C (src/BATTLE/BATTLE.PRG/771A8.c)
/// One wall-following step of `state`'s path with the passability `test`: 5 keep going, 14 detour to `next`, -1 stop or done.
pub fn func_800E2F5C(state: ?*v.movementPathState, test_: c.movementPathTest, mask: c_int) callconv(.c) c_int {
    const s = state.?;
    const t = test_.?;
    var path: v.movementPath = s.path;
    const bits = &path.bits;
    s.unk5 = 0;
    s.unk1 = s.unk199;
    done: {
        stop: {
            keep: {
                detour: {
                    if (psx.getBits(bits, "count") == 0) break :done;

                    const heading: c_int = psx.getBits(bits, "direction");
                    var direction: c_uint = @bitCast((if (psx.getBits(bits, "clockwise") != 0) heading -% 2 else heading +% 2) & 7);
                    const hit = c.func_800D98E8(@ptrCast(s), @bitCast(direction));
                    const blocker = hit -% 1;
                    if (hit != 0) {
                        if (blockerState(blocker).flags & 0x30 == 0 and blockerState(blocker).waitTimer == 0) {
                            const wait = psx.conv(u8, c.func_800E45D4(4) +% 4);
                            blockerState(blocker).waitTimer = wait;
                        }
                    }

                    if (s.unk4 == 0 and psx.getBits(bits, "changed") == 0 and t(@ptrCast(s), psx.getBits(bits, "direction")) == 0) {
                        const ahead = neighbour(s.tile, psx.getBits(bits, "direction"));
                        if (c.func_800D96C8(@ptrCast(s), s.tile.raw, ahead.raw, psx.getBits(bits, "direction")) == 0 or
                            c.func_800D954C(@ptrCast(s), ahead.raw) <= 0)
                        {
                            psx.setBits(bits, "changed", 1);
                            psx.setBits(bits, "clockwise", @intFromBool(psx.getBits(bits, "clockwise") == 0));
                            psx.setBits(bits, "visited", 0);
                            psx.setBits(bits, "direction", psx.getBits(bits, "direction") +% 4);
                        }
                    }

                    const boundX: c_int = @as(c_int, psx.getBits(bits, "boundX")) << 7;
                    const boundZ: c_int = @as(c_int, psx.getBits(bits, "boundZ")) << 7;
                    if (s.unk4 == 0) {
                        const x: c_int = @as(c_int, psx.getBits(&s.tile.p, "x")) +% psx.getBits(step(@bitCast(direction)), "dx");
                        const z: c_int = @as(c_int, psx.getBits(&s.tile.p, "z")) +% psx.getBits(step(@bitCast(direction)), "dz");
                        const grid: [*][32]u16 = @ptrFromInt(@as(usize, @as(*const u32, @ptrFromInt(0x1F8003C0)).*));
                        const bounds = v.g.D_800F58BC().*.?;
                        // mask: the C's shift is undefined outside 0-31.
                        if ((@as(c_int, psx.at(psx.at(grid, z), x).*) >> @intCast(mask)) & 1 != 0 or
                            x > bounds.maxX or z > bounds.maxZ or x < bounds.minX or z < bounds.minZ)
                        {
                            s.unkE8 = 0;
                            s.unkE7 = 0;
                            s.unkE4 = 0;
                            break :stop;
                        }
                        if (boundZ > 0) {
                            const center: c_int = (@as(c_int, psx.getBits(&s.destination.p, "z")) << 7) + 64;
                            if ((center < boundZ and s.z < boundZ) or (boundZ < center and boundZ < s.z)) break :done;
                        }
                        if (boundX > 0) {
                            const center: c_int = (@as(c_int, psx.getBits(&s.destination.p, "x")) << 7) + 64;
                            if ((center < boundX and s.x < boundX) or (boundX < center and boundX < s.x)) break :done;
                        }
                    } else {
                        if (boundZ > 0) {
                            const center: c_int = (@as(c_int, psx.getBits(&s.destination.p, "z")) << 7) + 64;
                            if ((boundZ < center and s.z < boundZ) or (center < boundZ and boundZ < s.z)) break :done;
                        }
                        if (boundX > 0) {
                            const center: c_int = (@as(c_int, psx.getBits(&s.destination.p, "x")) << 7) + 64;
                            if ((boundX < center and s.x < boundX) or (center < boundX and boundX < s.x)) break :done;
                        }
                    }

                    const cell = c.func_800E3D5C(@ptrCast(s), test_);
                    if (cell == psx.getBits(bits, "cell")) break :keep;
                    psx.setBits(bits, "cell", psx.conv(u4, cell));

                    var turn: c_int = -1;
                    if (psx.getBits(bits, "clockwise") != 0) turn = 1;
                    if (t(@ptrCast(s), (@as(c_int, psx.getBits(bits, "direction")) -% turn *% 2 -% turn) & 7) != 0 and
                        t(@ptrCast(s), (@as(c_int, psx.getBits(bits, "direction")) -% turn *% 2) & 7) != 0)
                    {
                        if (s.flags & 0x30 == 0 and s.waitTimer == 0) {
                            s.waitTimer = psx.conv(u8, c.func_800E45D4(4) +% 4);
                        }
                        break :stop;
                    }

                    psx.setBits(bits, "visited", psx.getBits(bits, "visited") | @as(u4, 1) << @intCast(psx.getBits(bits, "direction") >> 1));
                    if (psx.getBits(bits, "visited") == 15) break :stop;

                    direction = @bitCast((@as(c_int, psx.getBits(bits, "direction")) -% turn *% 2) & 7);
                    var i: c_int = 0;
                    while (i < 4) : (i += 1) {
                        if (t(@ptrCast(s), @bitCast(direction)) != 0) {
                            if (psx.getBits(bits, "direction") != direction) psx.setBits(bits, "changed", 1);
                            psx.setBits(bits, "direction", @intCast(direction));
                            break :keep;
                        }
                        const point = neighbour(s.tile, @bitCast(direction));
                        if (c.func_800D96C8(@ptrCast(s), s.tile.raw, point.raw, @bitCast(direction)) != 0 and
                            c.func_800D954C(@ptrCast(s), point.raw) > 0)
                        {
                            s.next = point;
                            if (psx.getBits(bits, "direction") != direction) psx.setBits(bits, "changed", 1);
                            psx.setBits(bits, "direction", @intCast(direction));
                            break :detour;
                        }
                        direction = @bitCast((@as(c_int, @bitCast(direction)) +% turn *% 2) & 7);
                    }
                    s.path = path;
                    return -1;
                }
                s.path = path;
                return 14;
            }
            s.path = path;
            return 5;
        }
        psx.setBits(bits, "stop", 1);
    }
    s.path = path;
    return -1;
}
