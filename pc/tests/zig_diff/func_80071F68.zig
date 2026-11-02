//! zig_diff spec: func_80071F68 -> func_80071F68.func_80071F68 (146C.c).
//! Inputs: D_800F19CC an arena block (room after it: the count is not
//! bounded) whose user is actor 0..3 and whose action is any of the 256;
//! the kind tables random (zero half the time); actors 0..15 present or not
//! with random allegiance (0x8000 sometimes); the status, part, area and
//! hit-preview stubs: statuses in each low-bit/0x3000000 combination, a
//! part or -1, in or out of the area; func_800A1AF8 writes the user's point,
//! func_8008631C a preview the func_8006F630 stub records; unk2984 0 or not;
//! func_8008E370's chests (0..3, open or not, of each kind); the user's
//! selected target and limb small so they match; unk298C 0xFF or not.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80071F68));

pub const classes = [_][:0]const u8{ "self", "aimed" };

pub const stubs = struct {
    pub fn func_800A1AF8(ctx: *zd.Ctx, id: anytype, a: anytype, out: anytype, b: anytype) void {
        _ = id;
        _ = a;
        _ = b;
        out[0].vx = ctx.value(i16);
        out[0].vy = ctx.value(i16);
        out[0].vz = ctx.value(i16);
        out[0].pad = ctx.value(i16);
    }
    pub fn func_800A0BE0(ctx: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        return ctx.edge(c_uint, &.{ 1, 1, 1, 0, 0x1000001, 0x2000001, 0x3000001, 0xFFFF_FFFF });
    }
    pub fn func_800A152C(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype) c_int {
        _ = a;
        _ = b;
        _ = cc;
        return ctx.edge(c_int, &.{ -1, 0, 1, 5 });
    }
    pub fn func_800C1564(ctx: *zd.Ctx, area: anytype, point: anytype) c_int {
        _ = area;
        _ = point;
        return if (ctx.value(u2) == 0) 0 else 1;
    }
    pub fn func_8008631C(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype, out: anytype) c_int {
        _ = a;
        _ = b;
        _ = cc;
        _ = d;
        const r: *v.D_800F19CC_t2 = @ptrCast(@alignCast(out.?));
        r.actionIndex = ctx.value(u16);
        r.unk4C[0].unk0.hp = ctx.value(i16);
        return 0;
    }
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const actor: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        actor.unk0.unk4.pad = ctx.value(i16);
        return 0;
    }
    pub fn func_8008E370(ctx: *zd.Ctx, count: anytype) ?*anyopaque {
        const n = ctx.edge(c_int, &.{ 0, 1, 3 });
        count[0] = n;
        const chests: [*]v._mpdRoomSection13 = @ptrCast(@alignCast(ctx.allocBytes(3 * @sizeOf(v._mpdRoomSection13), 16)));
        for (0..3) |k| {
            chests[k].unk10 = ctx.edge(i16, &.{ 0, 63, 64, -1 });
            chests[k].unkC = ctx.edge(i16, &.{ 1, 2, 3, 4, 0 });
            chests[k].unk8 = if (ctx.value(u1) == 0) 0 else 1;
        }
        return &chests[0];
    }
    pub fn func_8006F630(ctx: *zd.Ctx, entry: anytype, result: anytype, hit: anytype) void {
        _ = entry;
        const r: *const v.D_800F19CC_t2 = @ptrCast(@alignCast(result));
        const h: *const v._hitEntity_t = @ptrCast(@alignCast(hit.?));
        const seen = ctx.alloc([2]i32);
        seen[0] = r.actionIndex;
        seen[1] = h.unk0.hp;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const raw = ctx.allocBytes(@sizeOf(v.D_800F19CC_t) + 96 * @sizeOf(v.D_800F19CC_t5), 16);
    const block: *v.D_800F19CC_t = @ptrCast(@alignCast(raw));
    v.g.D_800F19CC().* = block;
    block.unk8.unk4.unk0.targetActor = if (ctx.chance(3)) 0 else ctx.range(u8, 1, 3);
    block.unk8.actionIndex = ctx.range(u16, 0, 255);
    block.unk2984 = if (ctx.chance(2)) 0 else 1;
    block.unk298C = if (ctx.chance(2)) 0xFF else 3;
    for (0..16) |k| {
        psx.globalMany(u8, "D_800E81B4")[k] = if (ctx.chance(2)) 0 else ctx.int(u8);
        psx.globalMany(u8, "D_800E81D4")[k] = if (ctx.chance(4)) 0 else ctx.int(u8);
        v.g.D_800E81C4()[k] = if (ctx.chance(2)) 0 else ctx.int(u8);
    }
    for (0..16) |k| {
        v.g.vs_battle_actors()[k] = null;
        if (k > 3 and ctx.chance(3)) continue;
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.stats.raw = psx.addr(ctx.alloc(v.vs_battle_actor2));
        actor.allegiance = if (ctx.chance(4)) ctx.int(u16) | 0x8000 else ctx.int(u16) & 0x7FFF;
        // the user's selected target and limb (the halves at +0x10, +0x12) small, so they match an entry
        @as(*align(1) u16, @ptrCast(@as([*]u8, @ptrCast(actor)) + 0x10)).* = ctx.range(u16, 0, 15);
        @as(*align(1) u16, @ptrCast(@as([*]u8, @ptrCast(actor)) + 0x12)).* = if (ctx.chance(2)) 0xFFFF else ctx.range(u16, 0, 5);
        v.g.vs_battle_actors()[k] = actor;
    }
    return .{ctx.int(c_int)};
}

pub fn classify(_: Args) usize {
    return if (v.g.D_800F19CC().*.?.unk8.unk4.unk0.targetActor == 0) 0 else 1;
}
