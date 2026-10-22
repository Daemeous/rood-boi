//! zig_diff spec: func_800DAED0 -> func_800DAED0.func_800DAED0 (71618.c).
//! Inputs: 0-4 actors, each with an id of 0-15 so its state comes from the
//! scratchpad table at 0x1F80037C (two actors may share a state, so a later
//! visit reads what an earlier call wrote). States have f14/f1D at zero, one,
//! 0x80 or 0xFF. The tile map (0x1F8003C0) is 256 x 32 u16 entries.
//! D_800F5878[0] is null or a state with depth/height at the i16 edges.
//! D_800F4538[0] has flags from the 0x180000 edges and a position[1] that
//! equals the state's half the time. D_800F16F4 is an i32 edge, and
//! func_800E45B4 returns it a quarter of the time.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DAED0));

pub const classes = [_][:0]const u8{ "empty", "idle", "active", "init" };

var first: ?*v.actionDispatchActor = null;
var states: [16]*v.actionDispatchState = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    first = null;
    const head = ctx.alloc(v.actionDispatchActor);
    v.g.vs_battle_actors()[0] = @ptrCast(head);

    const model = ctx.alloc(v.actionDispatchModel);
    model.flags = ctx.pick(u32, &.{ 0, 0x80000000, 0x80180000, 0x180000, 0x80000, 0x100000, 0xFFFFFFFF });
    model.position = .{ ctx.int(i16), ctx.int(i16), ctx.int(i16) };
    psx.put(&head.model, model);

    const gm = ctx.alloc(v.actionDispatchModel);
    gm.flags = ctx.pick(u32, &.{ 0, 0x80000000, 0x80180000, 0x180000, 0x80000, 0x100000, 0xFFFFFFFF });
    gm.position[1] = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    v.g.D_800F4538()[0] = @ptrCast(gm);

    for (0..16) |k| {
        const st = ctx.alloc(v.actionDispatchState);
        st.f14 = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
        st.f1D = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
        st.position[1] = if (ctx.chance(2)) gm.position[1] else ctx.int(i16);
        states[k] = st;
        @as(*u32, @ptrFromInt(zd.scratchpad + 0x37C + 4 * k)).* = psx.addr(st);
    }

    const tiles = ctx.allocBytes(256 * 32 * 2 + 64, 16);
    @as(*u32, @ptrFromInt(zd.scratchpad + 0x3C0)).* = psx.addr(tiles);

    if (ctx.chance(4)) {
        v.g.D_800F5878()[0] = null;
    } else {
        const hs = ctx.alloc(v.actionDispatchState);
        hs.depth = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        hs.height = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        v.g.D_800F5878()[0] = @ptrCast(hs);
    }

    v.g.D_800F16F4().* = ctx.pick(i32, &.{ 0, 1, -1, 0x7fffffff, -0x80000000 });

    const n = ctx.range(u8, 0, 4);
    var k: u8 = 0;
    while (k < n) : (k += 1) {
        const a = ctx.alloc(v.actionDispatchActor);
        a.id = ctx.range(i32, 0, 15);
        psx.put(&a.next, first);
        first = a;
    }
    psx.put(&head.next, first);
    return .{};
}

pub fn classify(_: Args) usize {
    const a = first orelse return 0;
    const st = states[@intCast(a.id)];
    if (st.f14 == 0) return 3;
    return if (st.f1D == 0) 1 else 2;
}

pub const stubs = struct {
    pub fn func_800E45B4(ctx: *zd.Ctx) c_int {
        if (ctx.value(u2) == 0) return v.g.D_800F16F4().*;
        return ctx.value(c_int);
    }

    pub fn func_800DEEFC(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = n;
        const st: *v.actionDispatchState = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) st.f14 = ctx.value(u8);
    }

    pub fn func_800E4624(ctx: *zd.Ctx, a: anytype, b: anytype, h1: anytype, h2: anytype) c_int {
        _ = a;
        _ = b;
        _ = h1;
        _ = h2;
        return ctx.value(c_int);
    }

    pub fn func_800DAD9C(ctx: *zd.Ctx, p: anytype) c_int {
        const st: *v.actionDispatchState = @ptrCast(@alignCast(p));
        if (ctx.value(u2) == 0) st.f1D = ctx.value(u8);
        if (ctx.value(u2) == 0) st.target48 = ctx.value(i32);
        return ctx.value(c_int);
    }

    pub fn func_800DB820(ctx: *zd.Ctx, p: anytype) void {
        const st: *v.actionDispatchState = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) st.f14 = ctx.value(u8);
        if (ctx.value(u1) == 1) st.height = ctx.value(i16);
    }

    pub fn func_800DB7B4(ctx: *zd.Ctx, p: anytype) void {
        const st: *v.actionDispatchState = @ptrCast(@alignCast(p));
        if (ctx.value(u2) == 0) st.f1D = ctx.value(u8);
        if (ctx.value(u1) == 1) st.distance = ctx.value(u32);
    }
};
