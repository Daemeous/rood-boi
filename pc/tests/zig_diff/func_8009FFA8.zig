//! zig_diff spec: func_8009FFA8 -> func_8009FFA8.func_8009FFA8 (30DB0.c).
//! Inputs: arg0 is a table index 0..16 whose object selects another table
//! entry through unk12 (0xFF, 16, 17, 0x80, 0..15 and so on). Every table
//! entry the C reads is set by prepare: arg0's object, and the selected
//! object when unk12 is 0..16, with nBones 0, 1, 0x80 or 0xFF. The C falls
//! off the end on the success path (no return), so its value there is the
//! same in both runs of one state and is compared like any other.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FFA8));

pub const classes = [_][:0]const u8{ "sel-ff", "sel-over-16", "no-bones", "ready" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const arg0 = ctx.range(c_int, 0, 16);
    const self = ctx.alloc(v.D_800F4538_t);
    self.unk0.nBones = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    self.unk0.unk12 = ctx.pick(u8, &.{ 0, 1, 15, 16, 17, 0x80, 0xFE, 0xFF });
    table[@intCast(arg0)] = self;

    const sel = self.unk0.unk12;
    if (sel <= 16) {
        // The selected entry: the same object when it selects arg0 itself.
        const target = if (@as(c_int, sel) == arg0) self else ctx.alloc(v.D_800F4538_t);
        target.unk0.nBones = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
        table[sel] = target;
    }
    const arg1 = ctx.pick(c_int, &.{ 0, -1, 1, 0x7fffffff, -0x80000000 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const self = table[@intCast(args[0])].?;
    const sel = self.unk0.unk12;
    if (sel == 0xFF) return 0;
    if (sel > 16) return 1;
    if (table[sel].?.unk0.nBones == 0) return 2;
    return 3;
}
