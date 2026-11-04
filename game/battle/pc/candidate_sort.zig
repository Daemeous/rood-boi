//! Sorting and shuffling an actor's action candidates (D2: 73B0C.s, 73C84.s, 7E05C.s;
//! oracle pc/src/battle_helpers.c, through its static sort_candidates).
const psx = @import("common").psx;
const c = @import("c_battle_helpers");

/// orig: candidate_score (pc/src/battle_helpers.c)
/// The score (first word) of the candidate at a guest address.
fn candidateScore(address: c_uint) c_int {
    const candidate: *align(1) c_int = @ptrFromInt(@as(usize, address));
    return candidate.*;
}

/// orig: sort_candidates (pc/src/battle_helpers.c)
/// K&R quicksort of the candidate pointers v[left..right] by score,
/// descending. The recursion into the right half masks `right` to a byte, as
/// the original does.
fn sortCandidates(v: [*]align(1) c_uint, left: c_int, right: c_int) void {
    if (left >= right) return;
    // Every index is >= 0: callers pass left 0 or 1 and the recursion only raises it.
    const l: usize = @intCast(left);
    const mid: usize = @intCast((left +% right) >> 1);
    var last: c_int = left;
    var swap = v[l];
    v[l] = v[mid];
    v[mid] = swap;
    var i: c_int = left +% 1;
    while (i <= right) : (i +%= 1) {
        if (candidateScore(v[l]) < candidateScore(v[@intCast(i)])) {
            last +%= 1;
            swap = v[@intCast(last)];
            v[@intCast(last)] = v[@intCast(i)];
            v[@intCast(i)] = swap;
        }
    }
    swap = v[@intCast(last)];
    v[@intCast(last)] = v[l];
    v[l] = swap;
    sortCandidates(v, left, last -% 1);
    sortCandidates(v, last +% 1, right & 0xff);
}

/// orig: func_800DC3CC (pc/src/battle_helpers.c)
/// Sorts the state's candidate pointers (0x430, count at 0x470) by score,
/// descending; more than 256 candidates are reported through pc_unimplemented,
/// which the original does not stop at.
pub fn sortActorCandidates(state: [*]u8) callconv(.c) void {
    const count = psx.byteAt(c_int, state, 0x470).*;
    if (count > 256) c.pc_unimplemented("candidate sort beyond byte-packed bounds");
    if (count != 0) sortCandidates(@ptrCast(psx.byteAt(c_uint, state, 0x430)), 0, count -% 1);
}

/// orig: func_800DC574 (pc/src/battle_helpers.c)
/// Gives the first `count` candidates (list at 0x430) one chance each, in
/// order, to take the lead: a candidate whose action byte (bits 5-12 of word
/// 1) is nonzero wins when rand() & 0x7F is below its weight (bits 19-26 of
/// word 3, capped at 0x4C); it swaps with the first entry and the rest are
/// re-sorted. rand() is drawn for every candidate visited, even a skipped one.
pub fn shuffleActorCandidates(state: [*]u8, count: c_int) callconv(.c) void {
    const list: [*]align(1) c_uint = @ptrCast(psx.byteAt(c_uint, state, 0x430));
    if (count > 16) c.pc_unimplemented("candidate shuffle beyond the 16-entry list");
    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        const roll: c_uint = @as(c_uint, @bitCast(c.rand())) & 0x7f;
        const candidate: [*]align(1) c_uint = @ptrFromInt(@as(usize, list[@intCast(i)])); // i < count, and a 0..16 list
        if (((candidate[1] >> 5) & 0xff) == 0) continue;
        const weight: c_uint = (candidate[3] >> 19) & 0xff;
        if (roll < (if (weight < 0x4c) weight else 0x4c)) {
            list[@intCast(i)] = list[0];
            list[0] = psx.addr(candidate);
            sortCandidates(list, 1, count -% 1);
            return;
        }
    }
}
