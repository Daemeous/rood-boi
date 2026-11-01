//! zig_diff spec: func_800AB4F0 -> func_800AB4F0.func_800AB4F0 (40564.c).
//! Inputs: a raw actor buffer (0x1840 bytes) with its page byte 0-23 (the
//! D_800E8F30 / D_800E8F90 bounds); per part a bounds width that is zero a
//! quarter of the time, a timer pair (state[4], state[5]) around the step
//! the tick speed (any sign, odd values for the signed /2) adds, positions
//! and velocities (both signs) placed so each window edge test is below, at
//! and above its limit, the bounce flag both ways, and sometimes a part's
//! height equal to the next part's width (the C's `bounds[2] != height`
//! skip). The actor's unk5AC bit 27 picks the /4 or /2 scale. Packets go to
//! an arena block whose guest address is in scratchpad slot 0; the
//! SetDrawMove stub copies the rect and both coordinates into the packet.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AB4F0));

pub const classes = [_][:0]const u8{ "scale4", "scale2", "skip-equal-height" };

pub const stubs = struct {
    pub fn SetDrawMove(ctx: *zd.Ctx, p: anytype, rect: anytype, x: anytype, y: anytype) void {
        _ = ctx;
        const pk: *v.DR_MOVE = @ptrCast(@alignCast(p));
        const r: *const v.RECT = @ptrCast(@alignCast(rect));
        const rx: u32 = @as(u16, @bitCast(r.x));
        const ry: u32 = @as(u16, @bitCast(r.y));
        const rw: u32 = @as(u16, @bitCast(r.w));
        const rh: u32 = @as(u16, @bitCast(r.h));
        pk.code[0] = rx | (ry << 16);
        pk.code[1] = rw | (rh << 16);
        pk.code[2] = @bitCast(@as(c_int, x));
        pk.code[3] = @bitCast(@as(c_int, y));
    }

    pub fn func_8007A824(ctx: *zd.Ctx, p: anytype) void {
        const pk: *v.DR_MOVE = @ptrCast(@alignCast(p));
        pk.tag = ctx.value(u32);
    }
};

var skip_case = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor: [*]u8 = ctx.allocBytes(0x1840, 16);
    actor[0x5BB] = ctx.range(u8, 0, 23);
    const flags: *align(1) u32 = @ptrCast(actor + 0x5AC);
    if (ctx.chance(2)) flags.* |= 0x8000000 else flags.* &= ~@as(u32, 0x8000000);

    const packets = ctx.allocBytes(4 * @sizeOf(v.DR_MOVE), 16);
    @as(*align(1) u32, @ptrFromInt(zd.scratchpad)).* = psx.addr(packets);

    const tick: c_int = ctx.pick(c_int, &.{ 0, 1, 2, 3, -1, -2, -3, 4, 5, 511, -511 });
    v.g.vs_gametime_tickspeed().* = tick;
    const step: u8 = psx.conv(u8, @divTrunc(tick, 2));

    skip_case = false;
    var i: usize = 0;
    while (i < 4) : (i += 1) {
        const bounds = actor + 0x61C + i * 8;
        const state = actor + 0x181C + i * 8;
        const part = actor + 0x5F4 + i * 4;
        bounds[2] = if (ctx.chance(4)) 0 else ctx.int(u8);
        // The timer: state[4] < state[5] + step (as u8) below, at and above.
        state[5] = ctx.int(u8);
        const sum: u8 = state[5] +% step;
        state[4] = switch (ctx.range(u8, 0, 3)) {
            0 => sum,
            1 => sum -% 1,
            2 => sum +% 1,
            else => ctx.int(u8),
        };
        if (ctx.chance(3)) {
            // Small values: the window edges are crossed on either side.
            bounds[0] = ctx.range(u8, 0, 40);
            bounds[1] = ctx.range(u8, 0, 40);
            bounds[2] = ctx.range(u8, 1, 60);
            bounds[3] = ctx.range(u8, 0, 60);
            part[2] = ctx.range(u8, 0, 30);
            part[3] = ctx.range(u8, 0, 30);
            state[2] = @bitCast(ctx.range(i8, -8, 8));
            state[3] = @bitCast(ctx.range(i8, -8, 8));
            // Positions so that x + dx (y + dy) lands on an edge sometimes.
            const dx: c_int = @as(i8, @bitCast(state[2]));
            const dy: c_int = @as(i8, @bitCast(state[3]));
            const tx: c_int = switch (ctx.range(u8, 0, 3)) {
                0 => @as(c_int, bounds[0]),
                1 => @as(c_int, bounds[0]) +% bounds[2] -% part[2],
                2 => @as(c_int, bounds[0]) +% ctx.range(c_int, -2, 2),
                else => @as(c_int, bounds[0]) +% bounds[2] -% part[2] +% ctx.range(c_int, -2, 2),
            };
            const ty: c_int = switch (ctx.range(u8, 0, 3)) {
                0 => @as(c_int, bounds[1]),
                1 => @as(c_int, bounds[1]) +% bounds[3] -% part[3],
                2 => @as(c_int, bounds[1]) +% ctx.range(c_int, -2, 2),
                else => @as(c_int, bounds[1]) +% bounds[3] -% part[3] +% ctx.range(c_int, -2, 2),
            };
            state[0] = psx.conv(u8, tx -% dx);
            state[1] = psx.conv(u8, ty -% dy);
        }
        bounds[6] = ctx.int(u8);
        if (i > 0 and ctx.chance(6)) {
            // The previous part's height equals this width (the C skips it).
            const prev = actor + 0x5F4 + (i - 1) * 4;
            if (bounds[2] != 0) prev[3] = bounds[2];
            skip_case = true;
        }
    }
    return .{@ptrCast(actor)};
}

pub fn classify(args: Args) usize {
    if (skip_case) return 2;
    const actor: [*]u8 = @ptrCast(args[0].?);
    const flags: *align(1) const u32 = @ptrCast(actor + 0x5AC);
    return if (flags.* & 0x8000000 != 0) 1 else 0;
}
