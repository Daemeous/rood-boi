//! zig_diff spec: func_8008C8A8 -> func_8008C8A8.func_8008C8A8 (146C.c).
//! Inputs: the guards (D_800F1BA4, no section 12, D_800F1D6E) each set
//! sometimes; the lights timer D_800F1D68 running (around 30 and 0, the
//! frame odd or even) or not; 0..4 section-12 objects with random flags
//! (hidden, parked: unk0_8 with unk4_1 all ones), height counters 0..7,
//! path state (moving or paused, the leg counter at and past the leg's
//! length, the stop at 0, at unk0_10, between) and coordinates at the 4096
//! edges of both signs; the tick speed 0..4; the collision stub (func_800A4494)
//! 0 or not; the scene stub (func_8009195C) an arena record or null.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008C8A8));

pub const classes = [_][:0]const u8{ "guarded", "lights", "objects" };

pub const ignore_return = "the C has bare returns and falls off its end (the game's object returns 0; the harness's copy leaves the register); its one caller, vs_battle_exec, ignores it";

pub const stubs = struct {
    pub fn func_800A4494(ctx: *zd.Ctx, id: anytype, point: anytype) c_int {
        _ = id;
        const seen = ctx.alloc(v.SVECTOR);
        seen.vx = point[0].vx;
        seen.vy = point[0].vy;
        seen.vz = point[0].vz;
        return if (ctx.value(u2) == 0) 1 else 0;
    }
    pub fn func_8009195C(ctx: *zd.Ctx, id: anytype) [*c]c_int {
        _ = id;
        if (ctx.value(u3) == 0) return null;
        return @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    }
};

fn coordinate(ctx: *zd.Ctx) i32 {
    return ctx.pick(i32, &.{ 0, 4095, 4096, -4095, -4096, -4097, 0x10000 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    v.g.D_800F1BA4().* = if (ctx.chance(8)) 1 else 0;
    v.g.D_800F1D6E().* = if (ctx.chance(8)) -1 else 0;
    v.g.D_800F1D68().* = if (ctx.chance(4)) ctx.edge(i16, &.{ 1, 2, 29, 30, 31, -1 }) else ctx.edge(i16, &.{ 0, -1 });
    v.g.vs_main_frameBuf().* = ctx.range(i32, 0, 1);
    v.g.D_800F1D6F().* = ctx.edge(u8, &.{ 0, 1 });
    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ 2, 2, 4, 3, 1, 0 });
    const n = ctx.range(u32, 0, 4);
    room.header.section12Len = n * @sizeOf(v._mpdRoomSection12) + ctx.edge(u32, &.{ 0, 0, 7 });
    if (ctx.chance(12)) {
        room.section12.raw = 0;
        return .{};
    }
    const objects: [*]v._mpdRoomSection12 = @ptrCast(@alignCast(ctx.allocBytes(4 * @sizeOf(v._mpdRoomSection12), 16)));
    for (0..4) |k| {
        const o = &objects[k];
        v.g.D_800F1D70()[k] = ctx.range(u8, 0, 7);
        if (ctx.chance(3)) psx.byteAt(u32, o, 4).* |= 0xFE;
        if (ctx.chance(2)) psx.setBits(o, "unk4_0", 0);
        psx.setBits(o, "unk4_16", ctx.edge(u16, &.{ 0, 1, 2, 0xFFFF }));
        psx.setBits(o, "unk0_27", ctx.range(u2, 0, 3));
        psx.setBits(o, "unk0_10", ctx.range(u2, 0, 3));
        psx.setBits(o, "unk0_9", @intFromBool(ctx.chance(4)));
        o.unkA8 = coordinate(ctx);
        o.unkAC = coordinate(ctx);
        o.unkB0 = coordinate(ctx);
        for (&o.unk8) |*p| {
            p.vx = coordinate(ctx);
            p.vy = coordinate(ctx);
            p.vz = coordinate(ctx);
            p.pad = ctx.edge(i32, &.{ 0, 1, 2, 3, -1, 0x10000 });
        }
    }
    psx.put(&room.section12, &objects[0]);
    return .{};
}

pub fn classify(_: Args) usize {
    const room = v.g.vs_battle_roomData();
    if (v.g.D_800F1BA4().* != 0 or room.section12.raw == 0 or v.g.D_800F1D6E().* != 0) return 0;
    return if (v.g.D_800F1D68().* > 0) 1 else 2;
}
