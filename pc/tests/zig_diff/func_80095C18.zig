//! zig_diff spec: func_80095C18 -> func_80095C18.func_80095C18 (2842C.c).
//! Inputs: the actor 0..15 and the kind: bit 3 clear (0..7, bit 7 with
//! 0..7 behind it, values no case takes) or set (the bubble). The screen
//! points func_800A1280 gives (each call's result chosen in prepare) sit on
//! every edge the C tests: the player 32 units either side (and exactly
//! at), vx at 15/16/17 and 303..305, vy at 7/8, 39..41, 207..209, the depth
//! vz - 16 at 63/64/65 and 2047/2048. The bubble's func_800A190C points
//! give |dx| + |dz| at 351..353 and 415..417; func_8009F858's frame is
//! -1, 0 or more; D_800F2260's counter is 7 mod 8 or not. The scratchpad's
//! packet cursor and OT point at arena blocks; vs_battle_setSprite hands
//! out arena packets; the packet cursor is in the arena or the scratchpad. pc_emote_record is a tentative definition of the unit
//! (.bss: outside the regions the harness compares), so every stub copies it
//! into an arena block: the trace's state hash then covers the record's
//! writes before each call.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80095C18));

pub const classes = [_][:0]const u8{ "no-point", "kind-0", "sprites", "arrow", "kind-4", "kind-5", "kind-6", "kind-7", "other-kind", "bubble-no-actor", "bubble-far", "bubble-hidden", "bubble" };

var results: [2]c_int = .{ 0, 0 };
var points: [2]v.SVECTOR = undefined;
var frame: c_int = 0;
var record_copy: *v.pc_classic_emote = undefined;
var record_start: v.pc_classic_emote = undefined;

/// The harness neither restores nor compares the record: the first call of a
/// run (before the C writes it) sets it to this iteration's start, and every
/// call copies it into the arena.
fn mirror(ctx: *zd.Ctx) void {
    if (ctx.calls == 0) v.g.pc_emote_record().* = record_start;
    record_copy.* = v.g.pc_emote_record().*;
}

pub const stubs = struct {
    pub fn func_800A1280(ctx: *zd.Ctx, a: anytype, l: anytype, out: anytype, m: anytype) c_int {
        mirror(ctx);
        _ = a;
        _ = l;
        _ = m;
        const k: usize = @min(ctx.calls, 1);
        const p: *v.SVECTOR = @ptrCast(@alignCast(out));
        p.* = points[k];
        return results[k];
    }
    pub fn func_800A190C(ctx: *zd.Ctx, a: anytype, l: anytype, out: anytype, m: anytype) c_int {
        mirror(ctx);
        _ = a;
        _ = l;
        _ = m;
        const k: usize = @min(ctx.calls, 1);
        const p: *v.SVECTOR = @ptrCast(@alignCast(out));
        p.* = points[k];
        return results[k];
    }
    pub fn RotTransPers(ctx: *zd.Ctx, v0: anytype, sxy: anytype, depth: anytype, flag: anytype) c_int {
        mirror(ctx);
        _ = v0;
        sxy.?.* = ctx.value(c_int);
        depth.?.* = ctx.value(c_int);
        flag.?.* = ctx.value(c_int);
        return ctx.value(c_int);
    }
    pub fn func_8009F858(ctx: *zd.Ctx, a: anytype) c_int {
        mirror(ctx);
        _ = a;
        return frame;
    }
    pub fn vs_battle_setSprite(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, ot: anytype) [*c]c_uint {
        mirror(ctx);
        _ = a;
        _ = b;
        _ = cc;
        _ = ot;
        return @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    }
    pub fn func_8009651C(ctx: *zd.Ctx, p: anytype, d: anytype) void {
        mirror(ctx);
        _ = p;
        _ = d;
    }
    pub fn func_80096254(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        mirror(ctx);
        _ = a;
        _ = b;
        return ctx.value(c_int);
    }
};

fn screenPoint(ctx: *zd.Ctx) v.SVECTOR {
    return .{
        .vx = ctx.pick(i16, &.{ 15, 16, 17, 160, 303, 304, 305, -1 }),
        .vy = ctx.pick(i16, &.{ 7, 8, 9, 39, 40, 41, 120, 207, 208, 209 }),
        .vz = ctx.pick(i16, &.{ 79, 80, 81, 1000, 2063, 2064, 2065 }),
        .pad = ctx.int(i16),
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    record_copy = ctx.alloc(v.pc_classic_emote);
    ctx.random.bytes(std.mem.asBytes(&record_start));
    const actor = ctx.range(c_int, 0, 15);
    const kind: c_int = switch (ctx.range(u8, 0, 5)) {
        0, 1 => ctx.range(c_int, 0, 7),
        2 => 0x80 | ctx.range(c_int, 0, 7),
        3 => ctx.edge(c_int, &.{ 0x10, 0x27, 0x7FFF_FFF0 }),
        else => 8 | ctx.edge(c_int, &.{ 0, 1, 0x80, 0x101 }),
    };
    results[0] = if (ctx.chance(10)) ctx.int(c_int) | 1 else 0;
    results[1] = if (ctx.chance(10)) ctx.int(c_int) | 1 else 0;
    if (kind & 8 == 0) {
        points[0] = screenPoint(ctx);
        points[1] = screenPoint(ctx);
        // The player 32 units either side, or exactly at the edge.
        if (ctx.chance(2)) points[1].vx = points[0].vx -% ctx.edge(i16, &.{ -33, -32, -31, 31, 32, 33 });
    } else {
        // func_800A190C: the actor first, then the player.
        points[0] = .{ .vx = ctx.int(i16) >> 2, .vy = ctx.int(i16), .vz = ctx.int(i16) >> 2, .pad = ctx.int(i16) };
        const total = ctx.edge(i32, &.{ 0, 351, 352, 353, 415, 416, 417, 1000 });
        const dx: i32 = ctx.range(i32, 0, total);
        const sx: i32 = if (ctx.chance(2)) -1 else 1;
        const sz: i32 = if (ctx.chance(2)) -1 else 1;
        points[1] = .{
            .vx = @truncate(@as(i32, points[0].vx) + sx * dx),
            .vy = ctx.int(i16),
            .vz = @truncate(@as(i32, points[0].vz) + sz * (total - dx)),
            .pad = ctx.int(i16),
        };
    }
    frame = ctx.edge(c_int, &.{ -1, -2147483648, 0, 0, 1, 7 });
    v.g.D_800F2260()[@intCast(actor)] = ctx.edge(u8, &.{ 7, 15, 255, 0, 3 });
    v.g.D_800F2274().* = ctx.int(i32);

    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    // The packet cursor in the arena, or in the scratchpad (an address with a top byte: the OT link drops it).
    const cursor: *anyopaque = if (ctx.chance(2)) @ptrFromInt(0x1F800100) else @ptrCast(ctx.allocBytes(64, 16));
    psx.put(&scratch.unk0, cursor);
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ctx.allocBytes(2048 * 4, 16))));
    return .{ actor, kind };
}

pub fn classify(args: Args) usize {
    const actor = args[0];
    var kind = args[1];
    if (kind & 8 == 0) {
        if (results[0] != 0 or results[1] != 0) return 0;
        if (kind & 0x80 != 0) {
            kind &= 7;
            if (@as(u32, @bitCast(kind -% 1)) >= 4) kind = 5;
        }
        return switch (kind) {
            0 => 1,
            1, 2, 3 => 2,
            4 => blk: {
                const p = points[0];
                const off = p.vx < 16 or p.vx > 304 or p.vy < 8 or p.vy > 208 or ((p.vx < 16 or p.vx > 304) and p.vy < 40);
                break :blk if (off) 3 else 4;
            },
            5 => 5,
            6 => 6,
            7 => 7,
            else => 8,
        };
    }
    if (results[0] != 0) return 9;
    if (results[1] != 0) return 10;
    // Whether the actor is far is decided against D_800F2274 after the update: count both as "far" or not by the bit after.
    const d: i32 = @intCast(@abs(@as(i32, points[1].vx) - points[0].vx) + @abs(@as(i32, points[1].vz) - points[0].vz));
    var flags = v.g.D_800F2274().*;
    const b = @as(i32, 1) << @intCast(actor);
    if (d < 352) flags &= ~b;
    if (d > 416) flags |= b;
    if ((flags >> @intCast(actor)) & 1 != 0) return 10;
    return if (frame < 0) 11 else 12;
}
