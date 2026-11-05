//! zig_diff spec: pc_charge_skip_name -> charge_skip.keepName (146C.c, charge_skip.inc).
//! Inputs: `now` 0 or 1; the window held or not; its text allocated or not; BATTLE's mode 2
//! or not; the action block present or not; the time since the release against the hold
//! (equal and one off; pc_vsync_count is a stub returning 5000); text box 7's done bit.
//! func_800CB50C (the game's close) is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").keepName));

pub const classes = [_][:0]const u8{ "not-held", "window-gone", "kept-up", "closed" };

pub const stubs = struct {
    pub fn pc_vsync_count(ctx: *zd.Ctx) c_uint {
        _ = ctx;
        return 5000;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.charge_skip_held().* = ctx.edge(c_int, &.{ 0, 1, 1, 1, 1, 9 });
    v.g.nameWindowText().* = if (ctx.chance(5)) null else @ptrCast(@alignCast(ctx.allocBytes(128, 16)));
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 2, 2, 2, 1, 3 });
    v.g.D_800F19CC().* = if (ctx.chance(5)) null else ctx.alloc(v.D_800F19CC_t);
    const held_vsyncs: c_int = ctx.edge(c_int, &.{ 0, 1, 30, 120, -1, std.math.minInt(c_int) });
    v.g.charge_skip_held_vsyncs().* = held_vsyncs;
    // elapsed = 5000 - held_at: at the hold, one under, one over
    v.g.charge_skip_held_at().* = 5000 -% @as(c_uint, @bitCast(held_vsyncs +% ctx.edge(c_int, &.{ 0, 0, -1, 1, 7, -7 })));
    psx.setBits(&v.g.textBoxes()[7].unk0, "done", ctx.edge(u1, &.{ 0, 1, 1 }));
    return .{ctx.edge(c_int, &.{ 0, 0, 1, 2 })};
}

pub fn classify(args: Args) usize {
    if (v.g.charge_skip_held().* == 0) return 0;
    if (v.g.nameWindowText().* == null) return 1;
    if (args[0] != 0 or v.g.D_800F196C().* != 2 or v.g.D_800F19CC().* == null) return 3;
    const elapsed: c_uint = 5000 -% v.g.charge_skip_held_at().*;
    if (elapsed < @as(c_uint, @bitCast(v.g.charge_skip_held_vsyncs().*)) or psx.getBits(&v.g.textBoxes()[7].unk0, "done") == 0) return 2;
    return 3;
}
