//! zig_diff spec: func_80092E14 -> func_80092E14.func_80092E14 (2842C.c).
//! Inputs: an output block and a source block in the arena, both valid (the C
//! has no null test, so neither argument is ever null). The source's eight
//! halfwords are drawn from edge values (0, 1, 0x7FFF, 0x8000, 0xFFFF) half
//! the time and any value otherwise, so the signed and unsigned reads and the
//! sign bit are all exercised. The C has no branches and no calls; the output
//! block's other fields are left as the random fill, so a port that writes
//! outside unk24..unk32 is caught by the state comparison.
//! Classes: whether any source halfword has its sign bit set.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092E14));

pub const classes = [_][:0]const u8{ "src-sign-bit", "src-no-sign-bit" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const out = ctx.alloc(v.func_80092F74_t);
    const src = ctx.alloc(v.func_80092F74_t2);
    const edges = [_]u16{ 0, 1, 0x7FFF, 0x8000, 0xFFFF };
    for (&src.unk4) |*e| {
        e.u16[0] = ctx.pick(u16, &edges);
        e.u16[1] = ctx.pick(u16, &edges);
    }
    return .{ out, src };
}

pub fn classify(args: Args) usize {
    for (args[1].?.unk4) |e| {
        if (e.u16[0] >= 0x8000 or e.u16[1] >= 0x8000) return 0;
    }
    return 1;
}
