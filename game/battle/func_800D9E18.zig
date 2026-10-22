//! Candidate movement choice for one target: whether the actor moves toward it
//! (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

// vs_gte_rsqrt: the unit's square-root call; the C compares its result as a signed int.
extern fn vs_gte_rsqrt(x: c_uint) c_int;

const Label = enum { normal, move, case2, case4, case7 };

/// orig: func_800D9E18 (src/BATTLE/BATTLE.PRG/71618.c)
/// Decides whether the candidate movement state moves toward the entry's target; returns 1 when it does.
pub fn func_800D9E18(state: ?*v.candidateMovementState, entry: ?*v.actionCandidateEntry) callconv(.c) c_int {
    const st = state.?;
    var id: c_int = undefined;
    var flag: c_int = undefined;
    var distance: c_uint = undefined;
    var limit: c_uint = undefined;
    var active: c_int = 0;

    if (st.count == 0) return 0;
    const ent = entry.?;
    id = @as(c_int, psx.getBits(ent, "targetId"));
    if (id == 16) return 0;
    // Scratchpad slot table at 0x1F80037C, one u32 per target.
    const slot: *const c_uint = @ptrFromInt(@as(usize, @as(c_uint, @bitCast(@as(c_int, 0x1F80037C) +% (id *% 4)))));
    if (slot.* == 0) return 0;

    var label: Label = switch (st.mode) {
        1 => .normal,
        2 => .case2,
        4 => .case4,
        7 => .case7,
        else => return 0,
    };

    // The C's labels and gotos as a loop over a plain switch (a labelled
    // `continue :dispatch` emits a writable jump table in the engine library's .data).
    dispatch: while (true) {
        switch (label) {
            .normal => {
                limit = 0x1F80;
                if ((@as(c_int, st.flags) & 8) == 0)
                    limit = psx.conv(c_uint, vs_gte_rsqrt(ent.distanceSquared) +% (@as(c_int, st.maxRange) << 5));
                limit = limit *% limit;
                if ((@as(c_int, st.targets[@intCast(id)].bytes[0]) >> 6) == 2) {
                    distance = ent.distanceSquared;
                    flag = 0;
                    active = 1;
                } else {
                    if (c.func_800DB8AC(@ptrCast(st), @ptrCast(ent)) != 0) {
                        distance = ent.distanceSquared;
                        active = 1;
                    } else {
                        distance = limit;
                        const minimum: c_uint = 0x64000;
                        if (distance < minimum) distance = minimum;
                    }
                    flag = 1;
                }
                distance = psx.conv(c_uint, vs_gte_rsqrt(distance));
                if (active != 0) {
                    v.g.D_800F58BC().*.?.unk24 +%= 1;
                    const target: usize = @intCast(psx.getBits(ent, "targetId"));
                    if (st.distances[@intCast(id)] < limit and (st.targets[target].raw & 1) != 0)
                        c.func_800D836C(@ptrCast(st));
                }
                {
                    label = .move;
                    continue :dispatch;
                }
            },
            .move => {
                if (c.func_800DB370(@ptrCast(st), id, @as(c_int, @bitCast(distance)), flag) != 0) return 1;
                return @as(c_int, @intFromBool(c.func_800DB74C(@ptrCast(st), id) != 0));
            },
            .case2 => {
                if (c.func_800DBCB4(@ptrCast(st), @ptrCast(ent)) == 0) {
                    label = .normal;
                    continue :dispatch;
                }
                if (psx.getBits(ent, "flag1") != 0) {
                    var point: [3]i16 = undefined;
                    point[0] = psx.conv(i16, (@as(c_int, st.destination.bytes[0]) << 7) +% 64);
                    point[2] = psx.conv(i16, (@as(c_int, st.destination.bytes[2]) << 7) +% 64);
                    point[1] = psx.conv(i16, c.func_800E42D4(@as(c_int, point[0]), @as(c_int, point[2])));
                    const pos: c_int = c.func_800E4624(
                        &point[0],
                        @ptrCast(&v.g.D_800F4538()[@intCast(id)].?.unk0.position),
                        @as(c_int, st.height),
                        @as(c_int, @as(*align(1) v.candidateMovementState, @ptrCast(v.g.D_800F5878()[@intCast(id)].?)).height),
                    );
                    distance = psx.conv(c_uint, vs_gte_rsqrt(@as(c_uint, @bitCast(pos))));
                    distance = @as(c_uint, @intFromBool(
                        @as(c_int, @bitCast(distance)) <
                            vs_gte_rsqrt(ent.distanceSquared) -% @as(c_int, psx.getBits(ent, "range")),
                    ));
                    if (distance != 0) {
                        label = .normal;
                        continue :dispatch;
                    }
                }
                const x: c_int = (@as(c_int, st.next.bytes[0]) << 7) +% 64;
                const z: c_int = (@as(c_int, st.next.bytes[2]) << 7) +% 64;
                const y: c_int = c.func_800E42D4(x, z);
                if (c.func_800E4660(
                    x -% @as(c_int, st.position[0]),
                    y -% @as(c_int, st.position[1]),
                    z -% @as(c_int, st.position[2]),
                ) <= 0xFFFF) {
                    st.target = psx.conv(u8, id);
                    c.func_800DB5B8(@ptrCast(st), st.destination.raw, 2);
                } else {
                    st.f1 = 1;
                    c.func_800DB5DC(@ptrCast(st), st.destination.raw, 256);
                }
                return 1;
            },
            .case4 => return c.func_800DB61C(@ptrCast(st), 0),
            .case7 => {
                c.func_800D82CC(@ptrCast(st));
                id = 0;
                flag = 1;
                if ((@as(c_int, st.targets[0].bytes[0]) >> 6) == 3) {
                    distance = 640;
                    {
                        label = .move;
                        continue :dispatch;
                    }
                }
                break :dispatch;
            },
        }
    }
    return 0;
}
