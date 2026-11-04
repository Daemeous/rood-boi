//! zig_diff spec: func_800E5240 -> pc.free_actor_route.freeActorRoute (D2: pc/src/actor_update.c).
//! Inputs: an actor id (0 to 16; 17 and above a sixth of the time, where the
//! C reports pc_unimplemented, whose stub returns, then reads past the
//! table: every slot of the 256-entry D_800F5878 is filled, null but the
//! actor's), D_800F58B8 zero a fifth of the time, the actor's state missing a
//! sixth of the time, and its route word at +0x18C zero or an arena address.
//! The stub of vs_main_freeHeapR records the freed pointer.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_update");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").freeActorRoute));

pub const classes = [_][:0]const u8{ "bad-id", "no-grid", "no-state", "no-route", "freed" };

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const id: c_uint = if (ctx.chance(6)) ctx.edge(c_uint, &.{ 17, 18, 40, 255 }) else ctx.range(c_uint, 0, 16);
    const table = v.g.D_800F5878();
    for (0..256) |slot| table[slot] = null;
    if (!ctx.chance(6)) {
        const state = ctx.allocBytes(0x200, 16);
        for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
        const route: u32 = if (ctx.chance(3)) 0 else @intCast(@intFromPtr(ctx.allocBytes(0x40, 16)));
        std.mem.writeInt(u32, state[0x18c..][0..4], route, .little);
        table[id] = @ptrCast(state);
    }
    v.g.D_800F58B8().* = if (ctx.chance(5)) 0 else ctx.edge(u32, &.{ 1, 0x80100000, 0xFFFFFFFF });
    return .{id};
}

pub fn classify(args: Args) usize {
    if (args[0] >= 17) return 0;
    if (v.g.D_800F58B8().* == 0) return 1;
    const state = v.g.D_800F5878()[args[0]] orelse return 2;
    const route = std.mem.readInt(u32, @as([*]u8, @ptrCast(state))[0x18c..][0..4], .little);
    return if (route == 0) 3 else 4;
}
