//! zig_diff spec: func_8009CE9C -> func_8009CE9C.func_8009CE9C (30DB0.c).
//! Inputs: arg0 on both sides of the bound (0, 1..3, 4, wrapping and high
//! values). The 17-slot object table is either all null or populated with
//! null slots mixed in. Populated slots point at arena blocks large enough
//! for every offset the C reads (up to 0x183E + 255). Each block's gate bytes
//! (0x183D + arg0 and 0x183E + D_800E8F2C) are set to pass or fail, and its
//! two unk180C[].unk1 bytes are drawn around 2. The callee func_800AB098 is a
//! recording stub that sometimes rewrites unk180C[].unk1 and D_800E8F2C.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009CE9C));

pub const classes = [_][:0]const u8{ "bad-arg", "no-object", "no-call", "call" };

const block_size = 0x2000;

pub const stubs = struct {
    pub fn func_800AB098(ctx: *zd.Ctx, p: anytype, k: anytype, x: anytype) void {
        _ = k;
        _ = x;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) a.unk180C[1].unk1 = ctx.value(u8);
        if (ctx.value(u1) == 1) a.unk180C[0].unk1 = ctx.value(u8);
        if (ctx.value(u1) == 1) v.g.D_800E8F2C().* = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg: c_uint = if (ctx.chance(4))
        ctx.pick(c_uint, &.{ 0, 4, 0xFFFF_FFFF, 0x8000_0000 })
    else
        ctx.edge(c_uint, &.{ 1, 2, 3 });
    const ok = arg -% 1 < 3;

    const d = v.g.D_800E8F2C();
    d.* = ctx.pick(u8, &.{ 0, 1, 2, 0xFE, 0xFF });

    var blocks: [4]*v.D_800F4538_t = undefined;
    for (&blocks) |*b| b.* = @ptrCast(@alignCast(ctx.allocBytes(block_size, 16)));

    const populated = !ctx.chance(6);
    for (v.g.D_800F4538()) |*slot| {
        if (!populated or ctx.chance(3)) {
            slot.* = null;
            continue;
        }
        const blk = blocks[ctx.range(usize, 0, 3)];
        slot.* = blk;
        const raw: [*]u8 = @ptrCast(blk);
        if (ok) raw[0x183D + @as(usize, arg)] = if (ctx.chance(4)) ctx.edge(u8, &.{ 0, 0xFE }) else 0xFF;
        raw[0x183E + @as(usize, d.*)] = if (ctx.chance(4)) 0xFF else ctx.edge(u8, &.{ 0, 1, 0xFE });
        blk.unk180C[0].unk1 = ctx.pick(u8, &.{ 0, 1, 2, 3, 0xFF });
        blk.unk180C[1].unk1 = ctx.pick(u8, &.{ 0, 1, 2, 3, 0xFF });
    }
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0];
    if (arg -% 1 >= 3) return 0;
    const d: usize = v.g.D_800E8F2C().*;
    var any = false;
    var called = false;
    for (v.g.D_800F4538().*) |maybe| {
        const t = maybe orelse continue;
        any = true;
        const raw: [*]u8 = @ptrCast(t);
        if (raw[0x183E + d] == 0xFF or raw[0x183D + @as(usize, arg)] != 0xFF) continue;
        if (t.unk180C[0].unk1 >= 2 or t.unk180C[1].unk1 >= 2) called = true;
    }
    if (!any) return 1;
    return if (called) 3 else 2;
}
