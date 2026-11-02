//! zig_diff spec: func_800B849C -> func_800B849C.func_800B849C (4A0A8.c).
//! Inputs: a 5-byte record in the arena. Bytes 1-2 are read as a short by
//! vs_battle_getShort, byte 3 is a table index (0-3, the table's four entries),
//! and byte 4 is the addend, with 0xFF as the "no value" case. The table
//! entries and the addend are drawn from edge bytes so that their sum
//! sometimes passes 255 (the C's sum is an int, not a byte). arg1 is any short.
//! The two callees are recording stubs: getShort returns any short and
//! func_800BFE50 any int, so the value passed on to func_8009FBD8 varies.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B849C));

pub const classes = [_][:0]const u8{ "addend-ff", "sum-wraps", "sum-fits" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const rec = ctx.allocBytes(8, 1);
    const table = v.g.D_800E9B58();
    for (0..4) |i| {
        table[i] = ctx.edge(u8, &.{ 0x00, 0x01, 0x80, 0xFE, 0xFF });
    }
    rec[0] = ctx.int(u8);
    rec[1] = ctx.int(u8);
    rec[2] = ctx.int(u8);
    rec[3] = ctx.edge(u8, &.{ 0, 1, 2, 3 });
    rec[4] = ctx.edge(u8, &.{ 0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF });
    return .{ rec, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const rec = args[0];
    if (rec[4] == 0xFF) return 0;
    const sum = @as(u32, v.g.D_800E9B58()[rec[3]]) + rec[4];
    return if (sum >= 256) 1 else 2;
}
