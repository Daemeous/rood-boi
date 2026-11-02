//! zig_diff spec: func_80074BAC -> func_80074BAC.func_80074BAC (146C.c).
//! Inputs: an actor 0..3 (arena, with arena stats: HP around 20/21, unk957
//! 0 or not) whose kept cell word matches the arena cell's low 24 bits or
//! not, its falling byte 0/1; the status stub's bits (falling 0x4000, lifted
//! 0x70000 with 0x20000000, frozen 0x1000000, busy 0x100000/0x400000) in
//! combinations; the cell stub a random _mpdRoomSection3; the object stub
//! none, a 0x10 pickup or another; the door stub none or 0..3 arena doors
//! whose flag (getStateFlag's stub) is under, at or over their level, in
//! this zone or another; func_8008D418's warp -1, 0 or a packed place; the
//! cell's raise (vy) around 16; the transition and battle state. The door
//! stubs record the door fields the C writes.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80074BAC));

pub const classes = [_][:0]const u8{ "blocked", "same-cell", "new-cell" };

var status: c_uint = 0;

fn recordDoor(ctx: *zd.Ctx, entry: anytype) void {
    const d: *const v._mpdRoomDoorSection_t = @ptrCast(@alignCast(entry.?));
    const seen = ctx.alloc([6]u8);
    const bytes: [*]const u8 = @ptrCast(d);
    seen.* = .{ bytes[0], bytes[1], bytes[2], bytes[3], bytes[5], bytes[7] & 0xFC };
}

pub const stubs = struct {
    pub fn func_800A0BE0(_: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        return status;
    }
    pub fn func_8008B764(ctx: *zd.Ctx, x: anytype, z: anytype, layer: anytype) ?*anyopaque {
        _ = x;
        _ = z;
        _ = layer;
        const room: *u32 = @ptrCast(@alignCast(ctx.allocBytes(4, 4)));
        room.* = ctx.value(u32);
        return room;
    }
    pub fn func_800BEC58(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype) c_short {
        _ = a;
        _ = b;
        _ = cc;
        _ = d;
        return ctx.edge(c_short, &.{ 0, 1, 2 });
    }
    pub fn func_8008D438(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype) ?*anyopaque {
        _ = a;
        _ = b;
        _ = cc;
        if (ctx.value(u2) == 0) return null;
        const object = ctx.alloc(v._mpdRoomSection9);
        object.unk6 = if (ctx.value(u1) == 0) 0x10 else ctx.value(i16);
        return object;
    }
    pub fn func_8008BAC8(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype) ?*anyopaque {
        _ = a;
        _ = b;
        _ = cc;
        if (ctx.value(u2) == 0) return null;
        const list: *v.D_800F1D08_t = @ptrCast(@alignCast(ctx.allocBytes(4 + 3 * 4, 16)));
        list.count = ctx.value(u2);
        for (0..3) |k| {
            const door = ctx.alloc(v._mpdRoomDoorSection_t);
            door.zndId = if (ctx.value(u1) == 0) @truncate(@as(u32, @bitCast(v.g._zoneContext().zndId))) else ctx.value(u8);
            psx.setBits(door, "unk4_0", ctx.edge(u8, &.{ 0, 4, 5, 6, 0xFF }));
            psx.put(psx.at(&list.values, k), door);
        }
        return list;
    }
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        return ctx.edge(u8, &.{ 4, 5, 6 });
    }
    pub fn func_8007C4E0(ctx: *zd.Ctx, entry: anytype, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        recordDoor(ctx, entry);
        return 1;
    }
    pub fn func_8007C5C0(ctx: *zd.Ctx, entry: anytype, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        recordDoor(ctx, entry);
        return 1;
    }
    pub fn func_8008D418(ctx: *zd.Ctx) c_int {
        return ctx.edge(c_int, &.{ -1, 0, 0x0005_0003, 0x00FF_00FF, -0x0001_0001, 0x0003_0000 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    const id = ctx.range(u8, 0, 3);
    const actor = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    stats.currentHP = ctx.edge(i16, &.{ 0, 1, 20, 21, 22, 100 });
    stats.unk957 = if (ctx.chance(2)) 0 else ctx.int(u8);
    actor.stats.raw = psx.addr(stats);
    v.g.vs_battle_actors()[id] = actor;
    const cell: [*]u8 = @ptrCast(ctx.allocBytes(8, 16));
    const word: *i32 = @ptrCast(@alignCast(cell));
    status = 0;
    if (ctx.chance(2)) status |= 0x4000;
    if (ctx.chance(3)) status |= ctx.edge(c_uint, &.{ 0x10000, 0x30000, 0x70000 });
    if (ctx.chance(4)) status |= 0x20000000;
    if (ctx.chance(10)) status |= 0x1000000;
    if (ctx.chance(4)) status |= ctx.edge(c_uint, &.{ 0x100000, 0x400000 });
    actor.unk2C.s32 = if (ctx.chance(2)) word.* else word.* ^ ctx.edge(i32, &.{ 1, 0x800000, 0x1000000 });
    actor.unk2C.u8[3] = if (ctx.chance(2)) @intFromBool(status & 0x4000 != 0) else ctx.edge(u8, &.{ 0, 1 });
    @as(*align(2) i16, @ptrCast(@alignCast(cell + 6))).* = ctx.edge(i16, &.{ 0, 16, 17, -1 });
    v.g._zoneContext().zndId = ctx.edge(i32, &.{ 3, 5 });
    v.g.vs_battle_screenTransitionStep().* = if (ctx.chance(8)) 1 else 0;
    v.g.D_800F196C().* = if (ctx.chance(6)) 3 else 1;
    return .{ id, cell };
}

pub fn classify(args: Args) usize {
    if (v.g.vs_battle_screenTransitionStep().* != 0 or status & 0x1000000 != 0) return 0;
    const actor = v.g.vs_battle_actors()[@intCast(args[0])].?;
    const word: *i32 = @ptrCast(@alignCast(args[1]));
    const falling: u8 = @intFromBool(status & 0x4000 != 0);
    if ((word.* & 0xFFFFFF) != (actor.unk2C.s32 & 0xFFFFFF) or falling != actor.unk2C.u8[3] or status & 0x70000 != 0) return 2;
    return 1;
}
