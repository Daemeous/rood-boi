//! zig_diff spec: func_8009998C -> func_8009998C.func_8009998C (30DB0.c).
//! Inputs: an object with a random index 0-15 and actorId 0-7. The
//! D_800F45E0 table holds random objects or nulls, with actorIds set so the
//! duplicate search finds a match (or none), a planted object in the inner
//! same-class search (actorId 4-7, the same or another actor than the
//! object's), and the already-set, no-object and null-table exits. The
//! D_800E8FB4 table and D_800E919C are set so the else path runs. Stubs:
//! vs_main_memcpy copies its bytes, func_800A6EE8 returns a prepared value
//! (so the unk1E match is reachable), func_800E6898 sometimes rewrites unk1E,
//! and func_8009A028 returns a prepared pointer or null.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009998C));

pub const classes = [_][:0]const u8{
    "already-set",
    "no-object",
    "found-in-table",
    "no-table-object",
    "inner-same-actor",
    "inner-other-actor",
    "high-actor",
    "low-actor",
};

var ret_1e: c_int = 0;
var ret_68: ?*anyopaque = null;
var mode: usize = 0;

pub const stubs = struct {
    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = ctx;
        const d: [*]u8 = @ptrCast(@as(?*anyopaque, dest).?);
        const s: [*]const u8 = @ptrCast(@as(?*const anyopaque, src).?);
        const n: usize = @as(usize, sz);
        @memcpy(d[0..n], s[0..n]);
    }

    pub fn func_8009A028(ctx: *zd.Ctx, a0: anytype, a1: anytype, a2: anytype) ?*anyopaque {
        _ = ctx;
        _ = a0;
        _ = a1;
        _ = a2;
        return ret_68;
    }

    pub fn func_800A6EE8(ctx: *zd.Ctx, a0: anytype, a1: anytype, a2: anytype, a3: anytype) c_int {
        _ = ctx;
        _ = a0;
        _ = a1;
        _ = a2;
        _ = a3;
        return ret_1e;
    }

    pub fn func_800E6898(ctx: *zd.Ctx, p: anytype) void {
        // Rewrites unk1E one time in 16 (reviewer: at 1 in 4 the planted object's +0x80 match was rare).
        if (ctx.value(u4) != 0) return;
        const s: *v.D_800F45E0_t = @ptrCast(@alignCast(@as(?*anyopaque, p).?));
        s.unk1E = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.vs_battle_objectData);
    const idx: usize = ctx.range(usize, 0, 15);
    arg.index = @intCast(idx);
    arg.actorId = ctx.range(u8, 0, 7);

    ret_1e = ctx.pick(c_int, &.{ 0, 1, -1, 0x7fff, -0x8000, 0x10000, 0x7fffffff, -0x80000000 });
    const s1e: i16 = @truncate(ret_1e -% (@as(c_int, arg.variant) << 7));

    const table = v.g.D_800F45E0();
    const already = ctx.chance(12);
    const noobj = !already and ctx.chance(10);
    const found = !already and !noobj and ctx.chance(3);
    const found_at: usize = (idx + ctx.range(usize, 1, 15)) % 16;

    for (table, 0..) |*e, i| {
        if (i == idx or (!(found and i == found_at) and ctx.chance(5))) {
            e.* = null;
            continue;
        }
        const o = ctx.alloc(v.D_800F45E0_t);
        var id: u8 = ctx.range(u8, 0, 7);
        if (found and i == found_at) {
            id = arg.actorId;
        } else if (id == arg.actorId) {
            id = (id + 1) & 7;
        }
        psx.setBits(&o.unk6C[8], "actorId", @intCast(id));
        e.* = o;
    }

    if (already) table[idx] = ctx.alloc(v.D_800F45E0_t);

    const target = ctx.alloc(v.D_800F45E0_t);
    psx.put(&arg.unk4, if (noobj) @as(?*anyopaque, null) else @as(?*anyopaque, target));

    var planted = false;
    var plant_same = false;
    if (arg.actorId > 4 and idx > 0 and !ctx.chance(5)) {
        const j = ctx.range(usize, 0, idx - 1);
        const o = ctx.alloc(v.D_800F45E0_t);
        const pid: u8 = if (ctx.chance(3)) arg.actorId else ctx.edge(u8, &.{ 4, 4, 5, 6, 7 });
        psx.setBits(&o.unk6C[8], "actorId", @intCast(pid));
        const flags: [4]u8 = std.mem.asBytes(&arg.unkC).*;
        const dst: *[4]u8 = @ptrCast(&o.unk5C);
        dst.* = flags;
        o.unk1E = @truncate(@as(i32, s1e) + 0x80);
        table[j] = o;
        planted = true;
        plant_same = pid == arg.actorId;
    }

    v.g.D_800E919C()[arg.actorId] = ctx.range(u8, 0, 2);
    for (0..3) |k| {
        v.g.D_800E8FB4()[k] = ctx.alloc(v.D_800E8FB4_t);
    }

    const block68 = ctx.alloc(v.D_800F45E0_unk68);
    psx.put(&block68.unk3C, @as(?*anyopaque, ctx.alloc(v.vs_battle_objectData)));
    const nulltab = ctx.chance(5);
    ret_68 = if (nulltab) @as(?*anyopaque, null) else @as(?*anyopaque, block68);

    mode = if (already)
        0
    else if (noobj)
        1
    else if (found)
        2
    else if (nulltab)
        3
    else if (planted)
        (if (plant_same) @as(usize, 4) else @as(usize, 5))
    else if (arg.actorId > 4)
        6
    else
        7;

    return .{arg};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
