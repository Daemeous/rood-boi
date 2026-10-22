//! The action candidate's target search for its current status
//! (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DA1D4 (src/BATTLE/BATTLE.PRG/71618.c)
/// Runs the target search for the candidate's status and returns 1 when an action was chosen.
pub fn func_800DA1D4(state: ?*v.actionCandidateState) callconv(.c) c_int {
    const st = state.?;
    const cst = psx.cptr(c.actionCandidateState, st);
    var id: c_int = -1;
    var flag: c_int = 0;
    var distance: c_int = 0;
    var nearest: c_uint = 0xFFFF_FFFF;

    sw: switch (@as(c_int, st.status)) {
        0 => {
            var entry = psx.get(st.entries[0]).?;
            id = @as(c_int, psx.getBits(entry, "targetId"));
            if (c.func_800D9E18(@ptrCast(st), @ptrCast(entry)) != 0) return 1;
            var i: c_int = 1;
            while (i < st.count) : (i +%= 1) {
                entry = psx.get(psx.at(&st.entries, i).*).?;
                if (@as(c_int, psx.getBits(entry, "threshold")) < @as(c_int, v.g.D_800F58BC().*.?.unk18) and
                    @as(c_int, psx.getBits(entry, "targetId")) != id)
                {
                    return c.func_800D9E18(@ptrCast(st), @ptrCast(entry));
                }
            }
            return 0;
        },
        1 => {
            c.func_800D82CC(@ptrCast(st));
            if ((@as(c_int, st.targets[0].flags) >> 6) == 3) {
                if (c.func_800DB370(cst, 0, 640, 1) != 0) return 1;
                if (c.func_800DB74C(cst, 0) != 0) return 1;
            }
            // targetId is a 4-bit field: always below 16, the targets array's length.
            id = @as(c_int, psx.getBits(psx.get(st.entries[0]).?, "targetId"));
            if ((@as(c_int, st.targets[@intCast(id)].flags) >> 6) == 3) {
                flag = 1;
                distance = 640;
            } else {
                if (st.count < 2) return 0;
                flag = 0;
                // vs_gte_rsqrt takes a C int: the u32 distance is converted as C converts it.
                distance = c.vs_gte_rsqrt(psx.conv(c_int, psx.get(st.entries[1]).?.distanceSquared));
            }
            if (c.func_800DB370(cst, id, distance, flag) != 0) return 1;
            if (c.func_800DB74C(cst, id) != 0) return 1;
            continue :sw 2;
        },
        2 => {
            c.func_800D82CC(@ptrCast(st));
            if ((@as(c_int, st.targets[0].flags) >> 6) == 3) {
                if (c.func_800DB370(cst, 0, 640, 1) != 0) return 1;
                if (c.func_800DB74C(cst, 0) != 0) return 1;
            }
            var actor: ?*v.vs_battle_actor = v.g.vs_battle_actors()[0];
            while (actor) |a| {
                const candidate: c_int = a.id;
                if ((@as(c_int, psx.at(&st.targets, candidate).flags) >> 6) == 3) {
                    const d: c_uint = psx.at(&st.targetDistancesSquared, candidate).*;
                    if (d < nearest) {
                        nearest = d;
                        id = candidate;
                    }
                }
                actor = psx.get(a.next);
            }
            if (id >= 0) {
                if (c.func_800DB370(cst, id, 640, 1) != 0) return 1;
                if (c.func_800DB74C(cst, id) != 0) return 1;
            }
        },
        else => {},
    }
    return 0;
}
