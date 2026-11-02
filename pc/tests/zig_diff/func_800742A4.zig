//! zig_diff spec: func_800742A4 -> func_800742A4.func_800742A4 (146C.c).
//! Inputs: D_800F1910.unk0 names an arena room section A whose point is
//! random (the shift by 7 wraps at 16 bits); func_8009E070's stub records
//! the point's x and z (the two words the game reads).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800742A4));

pub const classes = [_][:0]const u8{"any"};

pub const stubs = struct {
    pub fn func_8009E070(ctx: *zd.Ctx, actor: anytype, point: anytype, mode: anytype) void {
        _ = actor;
        _ = mode;
        const seen: *[2]i16 = @ptrCast(@alignCast(ctx.allocBytes(4, 4)));
        seen[0] = point[0];
        seen[1] = point[2];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const section = ctx.alloc(v._mpdRoomSectionA);
    psx.put(&v.g.D_800F1910().unk0, section);
    return .{};
}

pub fn classify(_: Args) usize {
    return 0;
}
