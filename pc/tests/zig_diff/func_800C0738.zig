//! zig_diff spec: func_800C0738 -> func_800C0738.func_800C0738 (573B8.c).
//! Inputs: the global D_800EB9B8 is either NULL (one time in three) or points
//! at a fresh arena D_800EB9B8_t whose bytes keep the random fill, so the
//! byte at unk10.unk0 is random and a store of 0 is observable. The C has no
//! calls and no ordered comparisons; its only test is the pointer null test.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C0738));

pub const classes = [_][:0]const u8{ "null", "non-null" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = if (ctx.chance(3)) null else obj;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800EB9B8().* == null) 0 else 1;
}
