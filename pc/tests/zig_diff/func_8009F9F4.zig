//! zig_diff spec: func_8009F9F4 -> func_8009F9F4.func_8009F9F4 (30DB0.c).
//! Inputs: a battle index 0-16 whose D_800F4538 entry is a random D_800F4538_t
//! or NULL, with random old unk17E4 fields (kind 0-3, unk1/unk2/unk3 zero, 1
//! or 0x80), and an unk64 argument whose kind is 0, 1 or other, whose unk1,
//! unk2 and unk3 are zero, 1, 0x80 or 0xFF. func_800AD62C writes both out
//! parameters with small values (0-3, so a stored zero is common).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F9F4));

pub const classes = [_][:0]const u8{
    "null-entry",
    "kind-zero",
    "kind-other",
    "kind-one-copy",
    "old-kind-0",
    "old-kind-1",
    "old-kind-2",
    "old-kind-other",
};

pub const stubs = struct {
    pub fn func_800AD62C(ctx: *zd.Ctx, a: anytype, out1: anytype, out2: anytype, b: anytype) void {
        _ = a;
        _ = b;
        out1[0] = @as(c_int, ctx.value(u2));
        out2[0] = @as(c_int, ctx.value(u2));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const idx = ctx.range(c_int, 0, 16);
    const obj = ctx.alloc(v.D_800F4538_t);
    obj.unk17E4.unk0 = ctx.pick(u8, &.{ 0, 1, 2, 3 });
    obj.unk17E4.unk1 = ctx.pick(u8, &.{ 0, 1, 0x80 });
    obj.unk17E4.unk2 = ctx.pick(u8, &.{ 0, 1, 0x80 });
    obj.unk17E4.unk3 = ctx.pick(u8, &.{ 0, 1, 0x80 });
    table[@intCast(idx)] = if (ctx.chance(8)) null else obj;

    const arg = ctx.alloc(v.D_800F4538_unk64);
    arg.unk0 = if (ctx.chance(2)) 1 else ctx.pick(u8, &.{ 0, 2, 3, 255 });
    arg.unk1 = ctx.pick(u8, &.{ 0, 1, 0x80 });
    arg.unk2 = ctx.pick(u8, &.{ 0, 1, 0x80 });
    arg.unk3 = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    return .{ idx, arg };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const obj = table[@intCast(args[0])] orelse return 0;
    const arg = args[1].?;
    if (arg.unk0 == 0) return 1;
    if (arg.unk0 != 1) return 2;
    if (arg.unk3 == 0) return 3;
    return switch (obj.unk17E4.unk0) {
        0 => 4,
        1 => 5,
        2 => 6,
        else => 7,
    };
}
