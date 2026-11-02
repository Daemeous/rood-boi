//! zig_diff spec: func_80077078 -> func_80077078.func_80077078 (146C.c).
//! Inputs: an arena actor whose unk44 is null or an arena block; random
//! flags; index and wepId at their truncation edges; arg4 with and without
//! 0x80 and random high bits.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80077078));

pub const classes = [_][:0]const u8{ "no-actor-id", "actor-id" };

pub const stubs = struct {
    /// Copies the fields the C sets into an arena record (the request is a
    /// stack local: the trace only tags its address).
    pub fn vs_battle_populateDataSlot(ctx: *zd.Ctx, data: anytype) c_int {
        const request: *const v.vs_battle_objectData = @ptrCast(@alignCast(data.?));
        const seen = ctx.alloc(v.vs_battle_objectData);
        seen.* = std.mem.zeroes(v.vs_battle_objectData);
        seen.dataType = request.dataType;
        seen.index = request.index;
        seen.modelId = request.modelId;
        seen.unkC = request.unkC;
        seen.unk4 = request.unk4;
        seen.actorId = request.actorId;
        seen.variant = request.variant;
        seen.material = request.material;
        if (request.actorId != 0xFF) seen.unk11 = request.unk11;
        return ctx.edge(c_int, &.{ -1, 0, 1, 15 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.vs_battle_actor);
    if (ctx.chance(4)) actor.unk44.raw = 0 else psx.put(&actor.unk44, ctx.alloc(v.D_800F4538_unk0));
    const flags = ctx.alloc(v.vs_battle_objectData_flags);
    return .{ actor, ctx.pick(c_int, &.{ 0, 1, 0x7F, 0x80, -1 }), ctx.pick(c_int, &.{ 0, 0x7FFF, 0x8000 }), flags, ctx.pick(c_int, &.{ 0, 0x80, 0xFF80, 0x7F, -0x81 }) };
}

pub fn classify(args: Args) usize {
    return if (args[4] & 0x80 != 0) 1 else 0;
}
