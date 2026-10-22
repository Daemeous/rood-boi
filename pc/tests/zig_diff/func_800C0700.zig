//! zig_diff spec: func_800C0700 -> func_800C0700.func_800C0700 (573B8.c).
//! Inputs: the global shape pointer D_800EB9B8 is either null (the C writes
//! through PS1 address 0, which the harness maps at 0x80000000) or an arena
//! object. arg0 is a random 16-byte block in the arena, or the shape's own
//! unk10 (a self copy). The shape's random bytes stay everywhere else, so the
//! copy into unk10 and the clear of unk2A show in the compared state.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C0700));

pub const classes = [_][:0]const u8{ "shape-null", "shape-arena", "self-copy" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const shape = ctx.alloc(v.D_800EB9B8_t);
    const src = ctx.alloc(v.D_800EB9B8_t2);
    const g = v.g.D_800EB9B8();
    if (ctx.chance(4)) {
        g.* = null;
        return .{src};
    }
    g.* = shape;
    if (ctx.chance(3)) return .{&shape.unk10};
    return .{src};
}

pub fn classify(args: Args) usize {
    const g = v.g.D_800EB9B8().*;
    if (g == null) return 0;
    if (args[0] == &g.?.unk10) return 2;
    return 1;
}
