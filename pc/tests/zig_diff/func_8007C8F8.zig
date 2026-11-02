//! zig_diff spec: func_8007C8F8 -> func_8007C8F8.func_8007C8F8 (146C.c).
//! Inputs: a model id at the 16-bit edges; the slot stub returns -1, 0, 1, 15.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8007C8F8));

pub const classes = [_][:0]const u8{"any"};

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
        return ctx.edge(c_int, &.{ -1, 0, 1, 15 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_int, &.{ 0, 0x7FFF, 0x8000, 0x12345, -1 })};
}

pub fn classify(_: Args) usize {
    return 0;
}
