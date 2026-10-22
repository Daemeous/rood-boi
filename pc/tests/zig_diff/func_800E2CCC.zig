//! zig_diff spec: func_800E2CCC -> func_800E2CCC.func_800E2CCC (771A8.c).
//! Inputs: a movement state and its context in the arena. The context guard bit
//! is set two times in three and the counter sits at 119-121 or 0/-1 often, so
//! the `counter++ > 120` test sees equal sides and the guard path is reached. The
//! angle sum's low ten bits are 512 a third of the time (the `> 512` equal case).
//! The position's low bytes come from 0, 1, 127, 128, 254 and 255, so the
//! scan's `(unsigned char)` wrap is exercised. The target is actor 0-15 or none
//! (16), with flag9 zero or not. The scratchpad direction table is all zero in a
//! quarter of the runs, which makes nx == x and nz == z, so the bounds compares
//! see equal sides. The stubs return height ties (0 and -32768 both shift to 0),
//! and they write the scratchpad bounds and state fields the C does not re-read
//! after the call, so a port that caches them still differs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E2CCC));

pub const classes = [_][:0]const u8{ "guard", "targeted", "flag9", "scan" };

fn stateOf(p: anytype) *v.movementRecoveryState {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub const stubs = struct {
    pub fn func_800DEEFC(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = n;
        const s = stateOf(p);
        if (ctx.value(u1) == 1) s.timer = ctx.value(u16);
    }

    pub fn func_800E678C(ctx: *zd.Ctx, p: anytype) void {
        const s = stateOf(p);
        if (ctx.value(u1) == 1) s.flags = ctx.value(u16);
    }

    pub fn func_800E4B10(ctx: *zd.Ctx, p: anytype) void {
        const s = stateOf(p);
        if (ctx.value(u1) == 1) s.counter = ctx.value(i32);
    }

    pub fn func_800D954C(ctx: *zd.Ctx, p: anytype, pos: anytype) c_int {
        _ = pos;
        const s = stateOf(p);
        if (ctx.value(u1) == 1) s.counter = ctx.value(i32);
        const r = ctx.value(u8);
        if (r < 60) return 0;
        if (r < 100) return -1;
        return ctx.value(c_int);
    }

    pub fn func_800E4320(ctx: *zd.Ctx, nx: anytype, nz: anytype) c_int {
        _ = nx;
        _ = nz;
        // The scan reads the bounds again on every step: a stub write must show.
        const bnd: *[2]u8 = @ptrFromInt(zd.scratchpad + 0x3DC);
        if (ctx.value(u3) == 0) bnd[ctx.value(u1)] = ctx.value(u8);
        return switch (ctx.value(u2)) {
            0 => 0,
            1 => 5,
            2 => -32768,
            3 => ctx.value(c_int),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.movementRecoveryState);
    const context = ctx.alloc(v.movementRecoveryContext);
    const actor = ctx.alloc(v.movementActorPosition);
    psx.put(&state.context, context);

    // The guard bit is set two times in three.
    context.flags = ctx.int(u32);
    if (ctx.chance(3)) {
        context.flags &= ~@as(u32, 0x200000);
    } else {
        context.flags |= 0x200000;
    }
    // The angle sum's low ten bits are 512 a third of the time.
    context.angle26 = ctx.int(i16);
    context.angle14 = if (ctx.chance(3))
        ctx.int(i16)
    else
        @bitCast(@as(u16, 512) -% @as(u16, @bitCast(context.angle26)));

    state.counter = ctx.pick(i32, &.{ 120, 120, 119, 121, 0, -1 });
    state.flags = ctx.int(u16);
    if (ctx.chance(2)) state.flags |= 0x36;
    state.flag9 = ctx.pick(u8, &.{ 0, 1, 0x80 });
    state.targetActor = if (ctx.chance(4)) 16 else ctx.range(u8, 0, 15);
    state.x = ctx.int(i16);
    state.z = ctx.int(i16);
    actor.x = ctx.int(i16);
    actor.z = ctx.int(i16);

    const xs = ctx.pick(u8, &.{ 0, 1, 127, 128, 254, 255 });
    const zs = ctx.pick(u8, &.{ 0, 1, 127, 128, 254, 255 });
    state.position.raw = (ctx.int(u32) & 0xFF00_FF00) | (@as(u32, zs) << 16) | @as(u32, xs);

    // Every entry of the actor table points at a valid actor.
    const tbl = v.g.D_800F4538();
    for (tbl) |*e| e.* = @ptrCast(@alignCast(actor));

    // The direction table and the bounds in the scratchpad.
    const tab: *[8]u8 = @ptrFromInt(zd.scratchpad + 0x3EC);
    const bnd: *[2]u8 = @ptrFromInt(zd.scratchpad + 0x3DC);
    const x: u8 = @truncate(state.position.raw);
    const z: u8 = @truncate(state.position.raw >> 16);
    if (ctx.chance(4)) {
        // Zero offsets: nx == x and nz == z, so the bounds compare sees equal sides.
        @memset(tab, 0);
        bnd[0] = if (ctx.chance(2)) x else x +% 1;
        bnd[1] = if (ctx.chance(2)) z else z +% 1;
    } else {
        bnd[0] = ctx.pick(u8, &.{ 0, 1, 128, 255 });
        bnd[1] = ctx.pick(u8, &.{ 0, 1, 128, 255 });
    }

    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    const context = psx.get(state.context).?;
    if ((context.flags & 0x200000) != 0 and state.counter > 120 and
        (state.flags & 6) != 0 and (state.flags & 0x30) != 0) return 0;
    if (state.targetActor != 16) return 1;
    if (state.flag9 != 0) return 2;
    return 3;
}
