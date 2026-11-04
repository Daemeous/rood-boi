//! zig_diff spec: func_800DBD80 -> pc.candidate_startable.candidateStartable (D2: pc/src/battle_movement.c).
//! Inputs, built so that every check passes up to the one under test (the
//! stage) and the later ones take random values: the candidate record (word 3
//! target nibble and weight bits 19-26, word 1 threshold bits 13-28, word 2
//! distance, word 4 options with bit 1 (range test), range bits 6-21 and
//! bits 2-3); the room halfword (D_800F58BC + 0x18) at, below and above the
//! threshold; the state's limit word (0x1E4 + target * 4: below, at and above
//! 0x19000 and the distance) and relation word (0x1A4 + target * 4, negative or
//! not, bits 2 and 27); the engagement table D_800F45E0 slot of the target set
//! or empty with the busy byte (0x13) and the stub of func_800DBCEC; the first
//! model's flag byte (0xA & 0x1F). The stubs of func_800DBCB4 (allowed) and
//! func_800DBCEC answer from the spec; vs_gte_rsqrt's argument is recorded.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").candidateStartable));

pub const classes = [_][:0]const u8{
    "no-weight",     "below-threshold", "not-allowed",  "engaged-limit", "engaged-busy", "engaged-hostile", "too-close",
    "range-too-far", "relation-clear",  "model-flags", "no-bit-2",      "no-bit-3",     "startable",
};

var stage: usize = 0;
var allowed: c_int = 1;
var hostile: c_int = 0;

pub const stubs = struct {
    pub fn func_800DBCB4(ctx: *zd.Ctx, state: anytype, candidate: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = candidate;
        return allowed;
    }
    pub fn func_800DBCEC(ctx: *zd.Ctx, state: anytype, target: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = target;
        return hostile;
    }
};

fn put16(p: [*]u8, off: usize, value: u16) void {
    std.mem.writeInt(u16, p[off..][0..2], value, .little);
}

fn put32(p: [*]u8, off: usize, value: u32) void {
    std.mem.writeInt(u32, p[off..][0..4], value, .little);
}

fn randomBlock(ctx: *zd.Ctx, size: usize) [*]u8 {
    const block = ctx.allocBytes(size, 16);
    for (block[0..size]) |*byte| byte.* = ctx.int(u8);
    return block;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    stage = ctx.range(usize, 0, 12);
    const state = randomBlock(ctx, 0x200);
    const candidate = randomBlock(ctx, 0x20);
    const room = randomBlock(ctx, 0x40);
    v.g.D_800F58BC().* = @intCast(@intFromPtr(room));
    const target: u32 = ctx.range(u32, 0, 15);

    // Word 3: target, weight.
    const weight: u32 = if (stage == 0) 0 else ctx.edge(u32, &.{ 1, 0xFF, 0x80, ctx.range(u32, 1, 255) });
    put32(candidate, 12, (ctx.int(u32) & ~@as(u32, 0x7F800000 | 0xF)) | (weight << 19) | target);
    // Word 1: threshold bits 13-28; the room value against it.
    const threshold: u16 = ctx.edge(u16, &.{ 1, 0x100, 0xFFFF, ctx.range(u16, 1, 0xFFFF) });
    put32(candidate, 4, (ctx.int(u32) & ~@as(u32, 0x1FFFE000)) | (@as(u32, threshold) << 13));
    put16(room, 0x18, if (stage == 1) threshold - 1 else threshold +| ctx.edge(u16, &.{ 0, 0, 1, 0x1000 }));
    allowed = if (stage == 2) 0 else ctx.edge(c_int, &.{ 1, 2, -1 });

    // The target's limit and the engagement table.
    const table = v.g.D_800F45E0();
    for (0..16) |i| table[i] = if (ctx.chance(2)) null else @ptrCast(randomBlock(ctx, 0x10));
    const engaged_stage = stage >= 3 and stage <= 5;
    if (engaged_stage) table[target] = @ptrCast(randomBlock(ctx, 0x10));
    var limit: i32 = ctx.edge(i32, &.{ 0, 1, 100, 0x19000, 0x18FFF, -1, -0x1000, 500 });
    if (stage == 3) limit = ctx.edge(i32, &.{ 0x19001, 0x7FFFFFFF, 0x20000 });
    state[0x13] = if (stage == 4) ctx.edge(u8, &.{ 1, 2, 0xFF }) else 0;
    hostile = if (stage == 5) ctx.edge(c_int, &.{ 1, 2, -1 }) else 0;
    if (table[target] != null and stage >= 6) {
        // Engaged targets pass only below the limit with no busy and no hostile.
        if (limit > 0x19000) limit = 0x19000;
    } else if (table[target] != null and stage < 3) {
        // The checks before the engagement ones return first: anything goes.
    }
    if (table[target] == null and stage >= 3) state[0x13] = ctx.int(u8);
    put32(state, 0x1e4 + 4 * target, @bitCast(limit));
    // Distance.
    var distance: i32 = limit +| ctx.edge(i32, &.{ 0, 0, 1, 1000 });
    if (stage == 6) distance = limit -| ctx.edge(i32, &.{ 1, 2, 1000 });
    put32(candidate, 8, @bitCast(distance));

    // Options: bit 1 (range test), range bits 6-21, bits 2 and 3.
    var options: u32 = ctx.int(u32) & ~@as(u32, 0x3FFFCE);
    var range: u32 = ctx.edge(u32, &.{ 0, 1, 10, 200, 0xFFFF, ctx.range(u32, 0, 0xFFFF) });
    var range_test = ctx.chance(2);
    if (stage == 7) {
        // limit < range * range
        limit = ctx.edge(i32, &.{ 0, 99, 100, 1000 });
        put32(state, 0x1e4 + 4 * target, @bitCast(limit));
        distance = limit +| ctx.edge(i32, &.{ 0, 1 });
        put32(candidate, 8, @bitCast(distance));
        range = ctx.edge(u32, &.{ 10, 11, 32, 100, 0xFFF });
        range_test = true;
        if (@as(i32, @bitCast(range *% range)) <= limit) range = 0x7FFF;
    } else if (stage >= 8 and range_test and range > 0) {
        // Pass: limit >= range * range (as a 32-bit signed square), or a wrapped square.
        const square: i32 = @bitCast(range *% range);
        if (limit < square) {
            limit = if (square > 0) square else limit;
            put32(state, 0x1e4 + 4 * target, @bitCast(limit));
            distance = limit +| ctx.edge(i32, &.{ 0, 1 });
            put32(candidate, 8, @bitCast(distance));
            if (limit < square and table[target] != null and limit > 0x19000) range = 0;
        }
    }
    options |= (range << 6) | (if (range_test) @as(u32, 2) else 0);
    // Relation word.
    var relation: u32 = ctx.int(u32) | 0x80000000;
    if (stage == 8) relation &= 0x7FFFFFFF;
    // Model flags.
    const model = randomBlock(ctx, 0x80);
    v.g.D_800F4538()[0] = @ptrCast(model);
    if (stage == 9) {
        model[0xa] = ctx.edge(u8, &.{ 1, 2, 4, 7, 8 | 1, 8, 0x18 & 8, 0x28, 0xFF });
        if (model[0xa] & 7 == 0 and model[0xa] & 0x18 != 8) model[0xa] |= 4;
    } else {
        model[0xa] = ctx.edge(u8, &.{ 0, 0x10, 0x18, 0xE0, 0xF0, 0x30, 0x20 });
    }
    // Bits 2 and 3.
    var opt2 = ctx.chance(2);
    var rel2 = ctx.chance(2);
    var opt3 = ctx.chance(2);
    var rel27 = ctx.chance(2);
    if (stage == 10) {
        opt2 = false;
        rel2 = false;
    } else if (stage >= 11 and !opt2 and !rel2) opt2 = true;
    if (stage == 11) {
        opt3 = false;
        rel27 = true;
    } else if (stage == 12 and !opt3 and rel27) rel27 = false;
    options = (options & ~@as(u32, 0xC)) | (if (opt2) @as(u32, 4) else 0) | (if (opt3) @as(u32, 8) else 0);
    relation = (relation & ~@as(u32, 4 | 0x08000000)) | (if (rel2) @as(u32, 4) else 0) | (if (rel27) @as(u32, 0x08000000) else 0);
    if (stage == 8) relation = ctx.edge(u32, &.{ 0, 0, 1, 4, 0x08000000, 0x7FFFFFFF });
    put32(candidate, 16, options);
    put32(state, 0x1a4 + 4 * target, relation);
    return .{ state, candidate };
}

pub fn classify(args: Args) usize {
    _ = args;
    return stage;
}
