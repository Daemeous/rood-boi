//! zig_diff spec: func_800765B0 -> func_800765B0.func_800765B0 (146C.c).
//! Inputs: slot 0xFF (with some, one, or none of 2..15 free) or a slot
//! 0..15 in use or not; random flags; actorId at the sign and byte edges;
//! the object data queue empty after 0..2 polls (the count lives in the
//! arena, restored for each run); the heap stub hands out an arena block.
//! The data-slot stub records the request's fields the C sets.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800765B0));

pub const classes = [_][:0]const u8{ "full", "searched", "in-use", "created" };

var polls: ?*u8 = null;

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        return @ptrCast(ctx.allocBytes(size, 16));
    }
    pub fn vs_main_bzero(_: *zd.Ctx, dest: anytype, size: anytype) void {
        const d: [*]u8 = @ptrCast(dest.?);
        @memset(d[0..@intCast(size)], 0);
    }
    pub fn vs_battle_getEmptyObjectDataSlot(_: *zd.Ctx) c_int {
        const left = polls.?;
        if (left.* == 0) return 0;
        left.* -= 1;
        return 1;
    }
    pub fn vs_battle_populateDataSlot(ctx: *zd.Ctx, data: anytype) c_int {
        const request: *const v.vs_battle_objectData = @ptrCast(@alignCast(data.?));
        const seen = ctx.alloc(v.vs_battle_objectData);
        seen.* = request.*;
        seen.dataAddr = 0;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    polls = @ptrCast(ctx.allocBytes(1, 1));
    polls.?.* = ctx.range(u8, 0, 2);
    const actors = v.g.vs_battle_actors();
    const full = ctx.chance(4);
    const last = ctx.chance(6); // only slot 15 free
    for (0..16) |k| actors[k] = if (full or last or ctx.chance(2)) ctx.alloc(v.vs_battle_actor) else null;
    if (last and !full) actors[15] = null;
    const slot = ctx.edge(c_int, &.{ 0xFF, 0xFF, 0xFF, 0, 1, 2, 15 });
    return .{ slot, ctx.pick(c_int, &.{ 0, 1, -1, 0xFF, 0x100, -0x100, -0xFF }), ctx.alloc(v.vs_battle_objectData_flags), ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const actors = v.g.vs_battle_actors();
    if (args[0] == 0xFF) {
        for (2..16) |k| if (actors[k] == null) return 1;
        return 0;
    }
    return if (actors[@intCast(args[0])] != null) 2 else 3;
}
