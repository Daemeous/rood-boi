//! zig_diff spec: func_800D4778 -> func_800D4778.func_800D4778 (5BF94.c).
//! Inputs: a random script. func_800CE83C gets a pointer to the caller's
//! stack (logged as a tag): its stub copies the three fields the C sets
//! (unk0, unk14, unk18; the rest is uninitialised in the C) into an arena
//! record, so they are compared; func_800D5198 returns any u16.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4778));

pub const classes = [_][:0]const u8{"spawn"};

var record: ?*[3]u32 = null;

pub const stubs = struct {
    pub fn func_800CE83C(ctx: *zd.Ctx, spawn: anytype) ?*anyopaque {
        _ = ctx;
        const s: *v.D_800F53B8_t2 = @ptrCast(@alignCast(spawn));
        record.?[0] = s.unk0.raw;
        record.?[1] = s.unk14.raw;
        record.?[2] = @bitCast(s.unk18);
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    record = @ptrCast(@alignCast(ctx.allocBytes(12, 16)));
    return .{ctx.alloc(v.D_800F53B8_t)};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
