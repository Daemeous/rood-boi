//! zig_diff spec: pc_charge_skip_trace -> charge_skip.trace (146C.c, charge_skip.inc).
//! Inputs: the camera mode, the loader state, object slot 0 in use or not, the cast flash, and
//! a chain of 0..4 background effect records; the buffer size at 0..300 (0, 1, 15, 16, 17 and
//! the sizes where the first or later record just fits). snprintf is the C library's.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").trace));

pub const classes = [_][:0]const u8{ "no-effects", "effects", "truncated" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g._cameraMode().* = ctx.pick(u32, &.{ 0, 4, 5, 6, 7 });
    v.g.D_800F531C().* = ctx.pick(u32, &.{ 0, 2, 5, 6, 8, 3 });
    v.g.vs_battle_objectDataSlots()[0].dataType = ctx.edge(u8, &.{ 0, 0, 1, 255 });
    v.g.D_800F1868().* = ctx.pick(c_int, &.{ 0, 1, 2, 3, -1 });
    var head: ?*v.D_800F53B8_t = null;
    var count = ctx.range(u8, 0, 4);
    while (count > 0) : (count -= 1) {
        const e = ctx.alloc(v.D_800F53B8_t);
        e.unkD1C.unk38 = ctx.pick(@TypeOf(e.unkD1C.unk38), &.{ 0, 1, 56, -1 });
        psx.setBits(e, "unk14_8", ctx.int(u3));
        e.next.raw = psx.addr(head);
        head = e;
    }
    v.g.D_800F54B0().* = head;
    return .{ ctx.allocBytes(512, 16), ctx.edge(c_int, &.{ 0, 1, 15, 16, 17, 18, 24, 25, 33, 40, 100, 300, 300 }) };
}

pub fn classify(args: Args) usize {
    const head = v.g.D_800F54B0().* orelse return 0;
    _ = head;
    return if (args[1] < 24) 2 else 1;
}
