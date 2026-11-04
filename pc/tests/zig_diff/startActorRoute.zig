//! zig_diff spec: func_800E527C -> pc.start_actor_route.startActorRoute (D2: pc/src/actor_update.c).
//! Inputs: an id (0 to 16; 17 and above a sixth of the time: the C reports
//! pc_unimplemented, whose stub returns, then reads past the table, so all
//! 256 slots of D_800F5878 are set), x and z (edges, any int: the target
//! word is z << 16 | x), D_800F58B8 zero a fifth of the time, the actor's
//! state missing a sixth of the time, its route word (+0x18C) nonzero a
//! third of the time, its own id byte (+0x88) any byte whose table slot holds
//! another block, and an allocator that fails a fifth of the time. The stubs
//! of the reservation releases, the allocator, memcpy and func_800E4288
//! record their arguments; the allocator hands out arena blocks.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_update");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startActorRoute));

pub const classes = [_][:0]const u8{ "bad-id", "no-grid", "no-state", "has-route", "alloc-failed", "allocated" };

var alloc_fails = false;

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        if (alloc_fails) return null;
        const n: usize = @intCast(size);
        return @ptrCast(ctx.allocBytes(n, 16));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const id: c_uint = if (ctx.chance(6)) ctx.edge(c_uint, &.{ 17, 18, 40, 255 }) else ctx.range(c_uint, 0, 16);
    alloc_fails = ctx.chance(5);
    const table = v.g.D_800F5878();
    for (0..256) |slot| table[slot] = null;
    if (!ctx.chance(6)) {
        const state = ctx.allocBytes(0x600, 16);
        for (state[0..0x600]) |*byte| byte.* = ctx.int(u8);
        const route: u32 = if (ctx.chance(3)) @intCast(@intFromPtr(state)) else 0;
        std.mem.writeInt(u32, state[0x18c..][0..4], route, .little);
        const other = ctx.allocBytes(0x600, 16);
        for (other[0..0x600]) |*byte| byte.* = ctx.int(u8);
        table[state[0x88]] = @ptrCast(other);
        table[id] = @ptrCast(state);
    }
    v.g.D_800F58B8().* = if (ctx.chance(5)) 0 else ctx.edge(u32, &.{ 1, 0x80100000, 0xFFFFFFFF });
    return .{
        id,
        ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF, 0xFFFF, 0x10000, -0x80000000 }),
        ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF, 0xFFFF, 0x10000, 0x7FFFFFFF }),
    };
}

pub fn classify(args: Args) usize {
    if (args[0] >= 17) return 0;
    if (v.g.D_800F58B8().* == 0) return 1;
    const state: [*]const u8 = @ptrCast(v.g.D_800F5878()[args[0]] orelse return 2);
    if (std.mem.readInt(u32, state[0x18c..][0..4], .little) != 0) return 3;
    return if (alloc_fails) 4 else 5;
}
