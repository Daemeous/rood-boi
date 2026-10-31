//! zig_diff spec: func_800D4E90 -> func_800D4E90.func_800D4E90 (5BF94.c).
//! Inputs: an arena D_800F53B8_t whose unkD1C.unk30 points at an arena
//! D_800F53B8_t4 (so the psx_get target is always valid; the C has no NULL
//! test to exercise), with unk4 zero or nonzero (negative and sign-bit
//! values included). func_800D46DC is a recording stub that sometimes
//! rewrites the target's unk4, so a port that misses the callee's effect
//! on state still mismatches.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4E90));

pub const classes = [_][:0]const u8{ "target-unk4-zero", "target-unk4-nonzero" };

pub const stubs = struct {
    pub fn func_800D46DC(ctx: *zd.Ctx, flag: anytype, p: anytype) void {
        _ = flag;
        const a: *v.D_800F53B8_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) {
            const target = psx.get(a.unkD1C.unk30).?;
            target.unk4 = ctx.value(i32);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F53B8_t);
    const target = ctx.alloc(v.D_800F53B8_t4);
    psx.put(&arg.unkD1C.unk30, target);
    target.unk4 = if (ctx.chance(2))
        0
    else
        ctx.edge(i32, &.{ 1, -1, 0x7fff_ffff, -0x8000_0000, 256, -256 });
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0].?;
    return if (psx.get(arg.unkD1C.unk30).?.unk4 == 0) 0 else 1;
}
