//! zig_diff spec: func_800AC540 -> func_800AC540.func_800AC540 (40564.c).
//! Inputs: one D_800F4538_t in the arena with a random 2-bit mode (every
//! value, so the switch default runs too), unk18 and unk1A zero half the time
//! (random otherwise, so the sign bit shows), unkA_5 and unk5AC_2 random (both
//! arms of each test), and the SVECTOR pad at -32768 a third of the time (the
//! 0x8000 sentinel) or any i16 (bit 11 and the sign bit both show up). The
//! callees are recording stubs. func_800A0204, func_800A9D90 and func_800AA620
//! may overwrite the pad through the spec's `target` pointer, which the C reads
//! back in case 2 after the first call.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
const port = @import("port");
pub const Args = std.meta.ArgsTuple(@TypeOf(port.func_800AC540));

pub const classes = [_][:0]const u8{
    "mode0-3",
    "case1-skip",
    "case1-run",
    "case2-skip",
    "case2-sentinel",
    "case2-run",
};

/// The object the current iteration passes; the stubs write through it.
var target: ?*v.D_800F4538_t = null;

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    target = obj;

    psx.setBits(obj, "unk5AC_0", ctx.edge(u2, &.{ 0, 1, 2, 3 }));
    obj.unk0.unk18 = if (ctx.chance(2)) 0 else ctx.int(u16);
    obj.unk0.unk1A = if (ctx.chance(2)) 0 else ctx.int(u16);
    psx.setBits(&obj.unk0, "unkA_5", ctx.int(u1));
    psx.setBits(obj, "unk5AC_2", ctx.int(u1));
    obj.unk5EC.pad = if (ctx.chance(3)) -32768 else ctx.int(i16);

    return .{ ctx.int(c_int), obj };
}

pub fn classify(args: Args) usize {
    const obj = args[1].?;
    const mode = psx.getBits(obj, "unk5AC_0");
    if (mode == 1) return if (obj.unk0.unk18 == 0) 2 else 1;
    if (mode == 2) {
        if (obj.unk0.unk1A != 0) return 3;
        return if (obj.unk5EC.pad == -32768) 4 else 5;
    }
    return 0;
}

pub const stubs = struct {
    pub fn func_800A0204(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) void {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        const t = target orelse return;
        if (ctx.value(u1) == 1) t.unk5EC.pad = ctx.value(i16);
    }

    pub fn func_800A9D90(ctx: *zd.Ctx, a: anytype, p: anytype, c: anytype) void {
        _ = a;
        _ = c;
        if (ctx.value(u1) == 1) {
            const s: *v.SVECTOR = @ptrCast(@alignCast(p));
            s.pad = ctx.value(i16);
        }
    }

    pub fn func_800AA620(ctx: *zd.Ctx, a: anytype, p: anytype, c: anytype) void {
        _ = a;
        _ = c;
        if (ctx.value(u1) == 1) {
            const s: *v.SVECTOR = @ptrCast(@alignCast(p));
            s.pad = ctx.value(i16);
        }
    }
};
