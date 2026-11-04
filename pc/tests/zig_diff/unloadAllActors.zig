//! zig_diff spec: func_800E6F1C -> pc.unload_all_actors.unloadAllActors (D2: pc/src/actor_cleanup.c).
//! Inputs: the actor list vs_battle_actors[0] empty or a chain of one to
//! three arena nodes (guest link words at +0, zero-terminated); the four
//! navigation globals random, so each reset to zero shows. The stubs of
//! func_800E7454 (one call per node, in order) and vs_main_freeHeap (the
//! D_800F58BC value) record their arguments.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_cleanup");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").unloadAllActors));

pub const classes = [_][:0]const u8{ "no-actors", "one-actor", "several-actors" };

var count: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    count = ctx.edge(usize, &.{ 0, 1, 2, 3 });
    var next: u32 = 0;
    var i: usize = 0;
    while (i < count) : (i += 1) {
        const node = ctx.allocBytes(0x40, 16);
        for (node[0..0x40]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(u32, node[0..4], next, .little);
        next = @intCast(@intFromPtr(node));
    }
    v.g.vs_battle_actors()[0] = if (next == 0) null else @ptrFromInt(next);
    v.g.D_800F58D0().* = ctx.int(u32);
    v.g.D_800F58B8().* = ctx.int(u32);
    v.g.D_800F58C0().* = ctx.int(u32);
    v.g.D_800F58BC().* = ctx.pick(u32, &.{ 0, 0x80100000 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (count == 0) 0 else if (count == 1) 1 else 2;
}
