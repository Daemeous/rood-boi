//! zig_diff spec: func_800C5360 -> func_800C5360.func_800C5360 (5BF94.c).
//! Inputs: a random group (edges 0..255, 256 and -1, so the signed compare and
//! the u8 unk9 truncation both show), an object in the arena whose unk40 points
//! at 0-12 arena elements with unk9 set to the group about a quarter of the
//! time, and the four u8 counters kept in range (CB0, CB1 in 0..4 and equal
//! about a fifth of the time; CB2, CB3 in 0..7). The counters stay in range so
//! the C writes only inside its [2][16] and [8] arrays. The C makes no calls,
//! so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C5360));

pub const classes = [_][:0]const u8{ "no-elements", "no-match", "match-else", "match-then" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800C56C0_t);
    const count = ctx.range(u8, 0, 12);
    obj.unk2C = count;

    const first: *v.func_800C5798_t = @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.func_800C5798_t) * 12, 16)));
    psx.put(&obj.unk40, first);
    const elems = @as([*]v.func_800C5798_t, @ptrCast(first));

    const group = ctx.edge(c_int, &.{ 0, 1, 2, 7, 127, 128, 255, 256, -1 });
    const group_byte: u8 = @truncate(@as(u32, @bitCast(group)));
    var i: usize = 0;
    while (i < count) : (i += 1) {
        elems[i].unk9 = if (ctx.chance(4)) group_byte else ctx.int(u8);
    }

    v.g.D_800EB9B8().* = obj;

    const cb1 = ctx.range(u8, 0, 4);
    const cb0 = if (ctx.chance(4)) cb1 else ctx.range(u8, 0, 4);
    v.g.D_800F4CB0().* = cb0;
    v.g.D_800F4CB1().* = cb1;
    v.g.D_800F4CB2().* = ctx.range(u8, 0, 7);
    v.g.D_800F4CB3().* = ctx.range(u8, 0, 7);

    return .{group};
}

pub fn classify(args: Args) usize {
    const obj = v.g.D_800EB9B8().*.?;
    if (obj.unk2C == 0) return 0;
    const elems = @as([*]v.func_800C5798_t, @ptrCast(@alignCast(psx.get(obj.unk40))));
    var any = false;
    var i: usize = 0;
    while (i < obj.unk2C) : (i += 1) {
        if (@as(c_int, elems[i].unk9) == args[0]) any = true;
    }
    if (!any) return 1;
    return if (v.g.D_800F4CB1().* > v.g.D_800F4CB0().*) 3 else 2;
}
