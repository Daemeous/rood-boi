//! zig_diff spec: func_800CD158 -> func_800CD158.func_800CD158 (5BF94.c).
//! Inputs: text box 0..7 at the top (y 0) or not, its edge and line count
//! at their edges (the line count clamps to 4..16 unsigned: a negative
//! count is large), the frame buffer 0 or 1, its state at its last (unk22 +
//! 1) or not; D_800F51B8 an arena OT. vs_battle_setSprite's stub returns one
//! arena packet (its two words per sprite: the state hash sees them).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CD158));

var packet: ?[*]u32 = null;

pub const stubs = struct {
    pub fn vs_battle_setSprite(ctx: *zd.Ctx, a: anytype, b: anytype, w: anytype, ot: anytype) [*c]u32 {
        _ = ctx;
        _ = a;
        _ = b;
        _ = w;
        _ = ot;
        return packet.?;
    }
};

pub const classes = [_][:0]const u8{ "top", "bottom", "bottom-last" };

pub fn prepare(ctx: *zd.Ctx) Args {
    packet = @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    v.g.D_800F51B8().* = @ptrCast(@alignCast(ctx.allocBytes(40 * 4, 16)));
    const id = ctx.range(c_int, 0, 7);
    const box = &v.g.textBoxes()[@intCast(id)];
    box.y = ctx.pick(i16, &.{ 0, 0, 1, 200 });
    box.unk2A = ctx.pick(i16, &.{ 0, 1, 4, 9, 13, 17, 20, 120, 127, 128, 143, 239, 240 });
    box.lineCount = ctx.pick(u8, &.{ 0, 1, 2, 3 });
    if (ctx.random.boolean()) box.state = @as(i16, box.unk22) + 1;
    v.g.vs_main_frameBuf().* = ctx.pick(i32, &.{ 0, 1 });
    return .{id};
}

pub fn classify(args: Args) usize {
    const box = &v.g.textBoxes()[@intCast(args[0])];
    if (box.y == 0) return 0;
    return if (box.state == @as(i16, box.unk22) + 1) 2 else 1;
}
