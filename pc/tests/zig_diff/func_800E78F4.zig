//! zig_diff spec: func_800E78F4 -> func_800E78F4.func_800E78F4 (7EE98.c).
//! Inputs: an arena func_800E78F4_t whose gate (unk470) is zero, negative or
//! positive, a guest pointer (unk430) to an arena t2 whose unk4 bits 13..28
//! are set to the global block's unk18 plus or minus one (so a < b, a == b
//! and a > b all occur often, with the bits above 28 and below 13 random), and
//! a global D_800F58BC block with edge values in unk18. The callee
//! func_800DBCB4 is a recording stub that sometimes redirects unk430 to
//! another t2, rewrites that t2's unk4 and the global's unk18, and returns 0
//! or any int, so the C's second read of unk430 and its read of unk18 after
//! the call are both exercised.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7EE98");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E78F4));

pub const classes = [_][:0]const u8{ "gate-closed", "gate-negative", "a-less", "a-equal", "a-greater" };

/// The redirect target the callee stub may switch unk430 to.
var alt: *v.func_800E78F4_t2 = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    const self = ctx.alloc(v.func_800E78F4_t);
    const t2 = ctx.alloc(v.func_800E78F4_t2);
    alt = ctx.alloc(v.func_800E78F4_t2);
    const blk = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = blk;
    blk.unk18 = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
    blk.unk14 = ctx.int(i32);

    self.unk470 = ctx.pick(i32, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000 });
    psx.put(&self.unk430, t2);

    const want: u16 = switch (ctx.range(u3, 0, 4)) {
        0 => blk.unk18 -% 1,
        1, 2 => blk.unk18,
        else => blk.unk18 +% 1,
    };
    t2.unk4 = (@as(u32, want) << 13) | @as(u32, ctx.int(u13)) | (@as(u32, ctx.int(u3)) << 29);
    if (ctx.chance(3)) t2.unk4 = ctx.int(u32);
    alt.unk4 = ctx.int(u32);

    return .{self};
}

pub fn classify(args: Args) usize {
    const self = args[0].?;
    if (self.unk470 == 0) return 0;
    if (self.unk470 < 0) return 1;
    const t2 = psx.get(self.unk430).?;
    const a: u16 = @truncate(t2.unk4 >> 13);
    const b = v.g.D_800F58BC().*.?.unk18;
    if (a < b) return 2;
    return if (a == b) 3 else 4;
}

pub const stubs = struct {
    pub fn func_800DBCB4(ctx: *zd.Ctx, arg: anytype, obj: anytype) c_int {
        const self: *v.func_800E78F4_t = @ptrCast(@alignCast(arg));
        const t2: *v.func_800E78F4_t2 = @ptrCast(@alignCast(obj));
        // The callee may redirect the object the C reads again for a.
        if (ctx.value(u2) == 0) psx.put(&self.unk430, alt);
        // The callee may change the t2 the C measures.
        if (ctx.value(u2) == 0) t2.unk4 = ctx.value(u32);
        // The callee may change the global whose unk18 the C reads after the call.
        if (ctx.value(u2) == 0) v.g.D_800F58BC().*.?.unk18 = ctx.value(u16);
        // The C tests the return: zero and nonzero, both signs.
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int);
    }
};
