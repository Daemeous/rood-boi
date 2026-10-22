//! zig_diff spec: func_800DD7CC -> func_800DD7CC.func_800DD7CC (74088.c).
//! Inputs: a state in the arena with edge coordinates (0, 0x40, -1, 32767,
//! -32768), a destination in the arena, a random 8-entry waypoint table
//! (bit 15 set on none, some or all entries, with edge low bits), and a
//! 32-word map block that the C only takes the address of. The two helper
//! calls are recording stubs: func_800E4660 returns distances drawn from
//! {0, 1, 0x7FFFFFFF, 0xFFFFFFFF}, so equal distances and the initial
//! closest (-1U) are hit often. func_800E42D4 sometimes rewrites a table
//! entry, which the C re-reads through its helper, and returns a small
//! signed height so the state subtraction cannot overflow.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DD7CC));

pub const classes = [_][:0]const u8{ "none-valid", "some-valid", "all-valid" };

pub const stubs = struct {
    pub fn func_800E42D4(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        if (ctx.value(u4) == 0) {
            const t = v.g.D_800F58C8().*.?;
            t[ctx.value(u3)] = ctx.value(u16);
        }
        return @as(c_int, ctx.value(i16));
    }

    pub fn func_800E4660(ctx: *zd.Ctx, dx: anytype, dy: anytype, dz: anytype) c_uint {
        _ = dx;
        _ = dy;
        _ = dz;
        const picks = [_]c_uint{ 0, 1, 0x7FFF_FFFF, 0xFFFF_FFFF };
        return picks[ctx.value(u2)];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.nearestWaypointState);
    const dest = ctx.alloc(v.nearestWaypointDestination);
    const table = ctx.alloc([8]u16);
    const map = ctx.alloc([32]u16);

    v.g.D_800F58C8().* = @ptrCast(table);
    v.g.D_800F58D0().* = @ptrCast(map);

    // 0: no valid entry, 1: some, 2: all.
    const mode = ctx.range(u8, 0, 2);
    for (table) |*e| {
        const valid = switch (mode) {
            0 => false,
            2 => true,
            else => ctx.chance(2),
        };
        const low = ctx.pick(u16, &.{ 0, 0xFFFF, 0x8000, 0x7FFF, 0x1F, 0x3FF, 0x3E0 });
        e.* = if (valid) (low | 0x8000) else (low & 0x7FFF);
    }

    state.x = ctx.pick(i16, &.{ 0, 0x40, -1, 32767, -32768 });
    state.y = ctx.pick(i16, &.{ 0, 0x40, -1, 32767, -32768 });
    state.z = ctx.pick(i16, &.{ 0, 0x40, -1, 32767, -32768 });

    return .{ state, dest };
}

pub fn classify(_: Args) usize {
    const table = v.g.D_800F58C8().*.?;
    var n: usize = 0;
    for (0..8) |i| {
        if ((table[i] & 0x8000) != 0) n += 1;
    }
    return if (n == 0) 0 else if (n == 8) 2 else 1;
}
