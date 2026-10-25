//! zig_diff spec: func_800C51B4 -> func_800C51B4.func_800C51B4 (5BF94.c).
//! Inputs: a func_800C56C0_t in the arena, reached through the global
//! D_800EB9B8, with a 256-entry func_800C5798_t table behind unk40. Both
//! branches on unk31 (zero or nonzero, including the 0x80 sign bit), every
//! mode in {0..5, -1, INT_MIN} so `mode == 1`, `mode != 3` and both bit tests
//! see every outcome, loop counts 0..255 (unk2C), the slot index unk2D around
//! 254/255, and group values on both sides of `>> 4`. The callees are
//! recording stubs that rewrite the loop bound, the slot's group and the
//! unk2D slot, so the C's re-reads of those fields are exercised.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C51B4));

pub const classes = [_][:0]const u8{
    "loop-mode3",
    "loop-empty",
    "loop-mode1",
    "loop-mode-other",
    "slot-none",
    "slot-mode0",
    "slot-mode1",
    "slot-mode2",
    "slot-mode3",
};

pub const stubs = struct {
    pub fn func_800C513C(ctx: *zd.Ctx, group: anytype, unkA: anytype) void {
        _ = group;
        _ = unkA;
        // The C does not re-read unk2D after this call, but the callee may move it.
        if (ctx.value(bool)) v.g.D_800EB9B8().*.?.unk2D = ctx.value(u8);
    }

    pub fn func_800C5164(ctx: *zd.Ctx, group: anytype, zero: anytype, unkA: anytype) void {
        _ = group;
        _ = zero;
        _ = unkA;
        if (ctx.value(bool)) {
            const o = v.g.D_800EB9B8().*.?;
            const e: [*]v.func_800C5798_t = @ptrCast(psx.get(o.unk40).?);
            e[ctx.value(u8)].unk9 = ctx.value(u8);
        }
    }

    pub fn func_800C518C(ctx: *zd.Ctx, group: anytype, data: anytype) void {
        _ = group;
        _ = data;
        // The loop re-reads its bound and each entry's group after this call.
        const o = v.g.D_800EB9B8().*.?;
        if (ctx.value(bool)) o.unk2C = ctx.value(u8);
        if (ctx.value(bool)) {
            const e: [*]v.func_800C5798_t = @ptrCast(psx.get(o.unk40).?);
            e[ctx.value(u8)].unk9 = ctx.value(u8);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800C56C0_t);
    const entries: [*]v.func_800C5798_t = @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.func_800C5798_t) * 256, 16)));
    for (0..256) |i| {
        entries[i].unk9 = ctx.pick(u8, &.{ 0, 1, 15, 16, 127, 128, 255 });
        entries[i].unkA = ctx.int(u8);
    }
    psx.put(&obj.unk40, &entries[0]);
    obj.unk2C = ctx.pick(u8, &.{ 0, 1, 2, 254, 255 });
    obj.unk2D = ctx.pick(u8, &.{ 0, 1, 254, 255 });
    obj.unk31 = if (ctx.chance(2)) 0 else ctx.edge(u8, &.{ 1, 2, 3, 0x80, 255 });
    obj.unk2F = ctx.int(u8);
    obj.unk30 = ctx.int(u8);
    obj.unk34 = ctx.int(i32);
    v.g.D_800EB9B8().* = obj;
    const mode = ctx.pick(c_int, &.{ 0, 1, 2, 3, 4, 5, -1, std.math.minInt(c_int) });
    return .{mode};
}

pub fn classify(args: Args) usize {
    const mode = args[0];
    const o = v.g.D_800EB9B8().*.?;
    if (o.unk31 != 0) {
        if (mode == 3) return 0;
        if (o.unk2C == 0) return 1;
        return if (mode == 1) 2 else 3;
    }
    if (o.unk2D == 255) return 4;
    return switch (mode & 3) {
        0 => 5,
        1 => 6,
        2 => 7,
        else => 8,
    };
}
