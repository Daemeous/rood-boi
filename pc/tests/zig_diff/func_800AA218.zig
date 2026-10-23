//! zig_diff spec: func_800AA218 -> func_800AA218.func_800AA218 (40564.c).
//! Inputs: an index 0-16 into the D_800F4538 table, with its entry null or an
//! actor in the arena (the C's only branch, `temp_s0 != 0`); a 4-byte
//! func_8006EBF8_t_fields in the arena with random bytes; a random arg2 that is
//! sometimes an edge value. The callee func_800AA290 is a recording stub that
//! sometimes rewrites the fields of arg1 and the actor's tile bytes, so the C
//! reads back what the call changed.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA218));

pub const classes = [_][:0]const u8{ "no-actor", "actor-low-index", "actor-high-index" };

pub const stubs = struct {
    pub fn func_800AA290(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype, arg3: anytype) void {
        _ = arg2;
        _ = arg3;
        // The C side hands over an opaque pointer; view it as the real layout.
        const f: *v.func_8006EBF8_t_fields = @ptrCast(@alignCast(arg1.?));
        if (ctx.value(u1) == 1) psx.setBits(f, "unk0_0", ctx.value(u8));
        if (ctx.value(u1) == 1) psx.setBits(f, "unk0_8", ctx.value(u8));
        if (ctx.value(u1) == 1) psx.setBits(f, "unk0_16", ctx.value(u8));
        const a = v.g.D_800F4538()[@intCast(arg0)];
        if (a) |actor| {
            if (ctx.value(u1) == 1) actor.unk0.currentTileX = ctx.value(u8);
            if (ctx.value(u1) == 1) actor.unk0.unk5D = ctx.value(u8);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx = ctx.range(u8, 0, 16);
    const tbl = v.g.D_800F4538();
    if (ctx.chance(4)) {
        tbl[idx] = null;
    } else {
        tbl[idx] = ctx.alloc(v.D_800F4538_t);
    }
    const arg1 = ctx.alloc(v.func_8006EBF8_t_fields);
    const arg2 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7fff_ffff, -0x8000_0000 });
    return .{ @as(c_int, idx), arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const tbl = v.g.D_800F4538();
    const idx: usize = @intCast(args[0]);
    if (tbl[idx] == null) return 0;
    return if (args[0] < 8) 1 else 2;
}
