//! zig_diff spec: func_800C06E0 -> func_800C06E0.func_800C06E0 (573B8.c).
//! Inputs: the global D_800EB9B8 is either NULL (the guard's false branch) or
//! points at a D_800EB9B8_t in the arena, whose first byte (unk0.unk0) is
//! zero or random. The unit's only effect is the byte store, so the classes
//! separate the null case from the two prior values of that byte. The
//! function calls nothing, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C06E0));

pub const classes = [_][:0]const u8{ "null", "object-byte-zero", "object-byte-random" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slot = v.g.D_800EB9B8();
    if (ctx.chance(3)) {
        slot.* = null;
    } else {
        const obj = ctx.alloc(v.D_800EB9B8_t);
        if (ctx.chance(2)) obj.unk0.unk0 = 0;
        slot.* = obj;
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const obj = v.g.D_800EB9B8().* orelse return 0;
    return if (obj.unk0.unk0 == 0) 1 else 2;
}
