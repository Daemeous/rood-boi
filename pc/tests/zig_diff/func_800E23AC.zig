//! zig_diff spec: func_800E23AC -> func_800E23AC.func_800E23AC (771A8.c).
//! Inputs: a patrol state in the arena whose row table is a 256-row block the
//! global points at, with random entries. Each run picks a row, a waypoint
//! index 0-7, a rate 0-31, a direction (0, 1, 7, 8, 0x80, 0xFF, 0xF8 as edges),
//! and a timer (0, 1, 2, 255). The row always has a bit-15 entry on the walk
//! the do-while takes, so it terminates. The tile's x and z are sometimes
//! equal to the current waypoint's fields (both, one, or neither), which
//! covers every arm of the outer `||`. func_800E45D4 returns zero about half
//! the time and a random c_int otherwise. func_800E50A0 may rewrite the
//! state's tile, which the C never reads back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E23AC));

pub const classes = [_][:0]const u8{ "arrived", "x-off", "z-off-timer-running", "z-off-turn" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.patrolWaypointState);
    const table = ctx.alloc([256][8]u16);
    const rows: [*][8]u16 = table;
    v.g.D_800F58D8().* = rows;

    state.row = ctx.int(u8);
    state.index = ctx.range(u8, 0, 7);
    state.rate = ctx.range(u8, 0, 31);
    state.direction = ctx.pick(u8, &.{ 0, 1, 7, 8, 0x80, 0xFF, 0xF8 });
    state.timer = ctx.pick(u8, &.{ 0, 1, 2, 255 });

    // Make the do-while terminate: a bit-15 entry on the walk from the index
    // in the forward and the reversed direction (the C may flip it).
    const row = &table[state.row];
    const k = ctx.range(u8, 1, 8);
    const d = state.direction & 7;
    const fwd = (state.index +% k *% d) & 7;
    const back = (state.index -% k *% d) & 7;
    row[fwd] |= 0x8000;
    row[back] |= 0x8000;

    // The tile matches the current waypoint's x and z about half the time each.
    const e: u16 = row[state.index];
    const x: u8 = if (ctx.chance(2)) @as(u8, @intCast(e & 31)) else ctx.int(u8);
    const z: u8 = if (ctx.chance(2)) @as(u8, @intCast((e >> 5) & 31)) else ctx.int(u8);
    psx.setBits(&state.tile.p, "x", x);
    psx.setBits(&state.tile.p, "z", z);

    return .{state};
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    const table = v.g.D_800F58D8().*.?;
    const row = &table[s.row];
    const e: u16 = row[s.index];
    const xm = @as(u16, psx.getBits(&s.tile.p, "x")) == (e & 31);
    const zm = @as(u16, psx.getBits(&s.tile.p, "z")) == ((e >> 5) & 31);
    if (xm and zm) return 0;
    if (!xm) return 1;
    if (s.timer != 0) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn func_800E45D4(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        if (ctx.value(u8) < 128) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800E50A0(ctx: *zd.Ctx, state: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) {
            const s: *v.patrolWaypointState = @ptrCast(@alignCast(state));
            s.tile.raw = ctx.value(u32);
        }
    }
};
