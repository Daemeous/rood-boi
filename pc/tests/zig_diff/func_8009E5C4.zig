//! zig_diff spec: func_8009E5C4 -> func_8009E5C4.func_8009E5C4 (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 (the 17-entry pointer table), with the
//! entry either null or a random-filled arena object whose animationId is one of
//! 0x63, 0xC7, 0x62, 0x64, 0x00, 0xFF, 0xE3 (0x63 with the sign bit) or 0x47. Every
//! branch is reached: the empty slot, the 0x63 and 0xC7 short-circuits, and the
//! other animation ids that call func_8009E700(arg0, 1). func_8009E700 is a void
//! callee the C never reads back after the call (both paths return or end), so
//! its recording stub changes nothing. Indices outside 0..16 are not generated:
//! they would read the globals next to the table.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E5C4));

pub const classes = [_][:0]const u8{ "null-slot", "anim-0x63", "anim-0xC7", "other-anim" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slots = v.g.D_800F4538();
    const idx = ctx.range(c_int, 0, 16);
    const obj = ctx.alloc(v.D_800F4538_t);
    obj.animationId = ctx.edge(u8, &.{ 0x63, 0xC7, 0x62, 0x64, 0x00, 0xFF, 0xE3, 0x47 });
    slots[@intCast(idx)] = if (ctx.chance(4)) null else obj;
    return .{idx};
}

pub fn classify(args: Args) usize {
    const obj = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    return switch (obj.animationId) {
        0x63 => 1,
        0xC7 => 2,
        else => 3,
    };
}
