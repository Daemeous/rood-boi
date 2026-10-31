//! zig_diff spec: func_800D5048 -> func_800D5048.func_800D5048 (5BF94.c).
//! Inputs: `previous` is null, points at an arena object (raw address without
//! the sign bit), or points into PS1 low RAM at 0x80000100 (raw address with
//! the sign bit set). The target's unk14_11 bitfield is random, 0 (the
//! decrement wraps to 31) or any other value. unk8 is 0, 1 or 255, so a guard
//! that reads unk8 differs. The C has no callees, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5048));

pub const classes = [_][:0]const u8{ "previous-null", "previous-arena", "previous-lowram" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const self = ctx.alloc(v.D_800F53B8_t);
    self.unk8 = ctx.pick(u8, &.{ 0, 1, 255 });

    const target: *align(1) v.D_800F53B8_t = if (ctx.chance(3))
        psx.byteAt(v.D_800F53B8_t, psx.lowRam(u8), 0x100)
    else
        ctx.alloc(v.D_800F53B8_t);

    if (ctx.chance(4)) {
        psx.put(&self.previous, null);
    } else {
        psx.put(&self.previous, target);
        if (ctx.chance(3)) psx.setBits(target, "unk14_11", 0);
    }
    return .{self};
}

pub fn classify(args: Args) usize {
    const self = args[0].?;
    const target = psx.get(self.previous) orelse return 0;
    return if (psx.addr(target) >= 0x8000_0000) 2 else 1;
}
