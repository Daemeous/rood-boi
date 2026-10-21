//! zig_diff spec: func_800A1648 -> func_800A1648.func_800A1648 (38C1C.c).
//! Inputs: an actor index 0-15. The D_800F4538 entry is an arena actor or
//! null, and the D_800F45E0 fallback is either null (the C returns -1) or the
//! same arena object, read at the same unk68 offset. The actor's model is an
//! arena block of sixteen armatures whose unk7 comes from edges (nibbles zero,
//! 0x80 bit set, 0xFF) or random bytes. arg1 runs from -4 (before the first
//! armature) to 15, and arg2 is 0, 1 or 2. The switch has no default in the
//! source, and an arg2 outside 0..2 leaves the C's v uninitialised, so the
//! spec keeps arg2 inside 0..2 and waives the switch's implicit default arc.

const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1648));

pub const classes = [_][:0]const u8{
    "no-actor",
    "fallback-entry",
    "case0",
    "case1-zero",
    "case1-nonzero",
    "case2-zero",
    "case2-nonzero",
};

const armature_count = 16;
const model_size = @offsetOf(v.D_800F4538_unk68, "armatures") + armature_count * @sizeOf(v._armature_t);
const edges7 = [_]u8{ 0x00, 0x0F, 0x10, 0x1F, 0x20, 0x80, 0xF0, 0xFF };

var last_class: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = ctx.range(c_int, 0, 15);
    const arg1: c_int = ctx.range(c_int, -4, 15);
    const arg2: c_int = ctx.range(c_int, 0, 2);
    const idx: usize = @intCast(arg0);

    const actor = ctx.alloc(v.D_800F4538_t);
    const model_bytes = ctx.allocBytes(model_size, 16);
    const model: *v.D_800F4538_unk68 = @ptrCast(@alignCast(model_bytes));
    for (0..armature_count) |k| {
        const arm = psx.at(&model.armatures, k);
        arm.unk7 = ctx.pick(u8, &edges7);
    }
    psx.put(&actor.unk0.unk68, model);

    const chosen = psx.at(&model.armatures, arg1);
    const t4 = v.g.D_800F4538();
    const t45 = v.g.D_800F45E0();
    const path = ctx.random.uintLessThan(u8, 8);
    if (path == 0) {
        t4[idx] = null;
        t45[idx] = null;
        last_class = 0;
    } else if (path < 3) {
        t4[idx] = null;
        t45[idx] = @ptrCast(actor);
        last_class = 1;
    } else {
        t4[idx] = actor;
        t45[idx] = null;
        last_class = if (arg2 == 0)
            2
        else if (arg2 == 1)
            (if ((chosen.unk7 >> 4) == 0) @as(usize, 3) else 4)
        else
            (if ((chosen.unk7 & 0xF) == 0) @as(usize, 5) else 6);
    }
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return last_class;
}
