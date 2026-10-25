//! zig_diff spec: func_8009FD5C -> func_8009FD5C.func_8009FD5C (30DB0.c).
//! Inputs: an index 0-16 into D_800F4538 (16 is only used with a present
//! D_800F4538 entry, since D_800F45E0 has 16 slots), a random arg1 (its bit 1
//! reaches the flag byte through the mask), and an arg2 from the edges -1, -2,
//! 0..9 and the i32 extremes, so the unsigned `>= 8u` test sees negatives and
//! sees 8 as equal. The D_800F4538 object (or the D_800F45E0 one when the
//! table_a3 entry is null) gets a flag byte that is zero about half the time
//! and otherwise 1, 0x80 or 0xFF. The vs_main_loadClut stub sometimes rewrites
//! that flag byte, which the C reads back for the arg2 step.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FD5C));

pub const classes = [_][:0]const u8{ "no-object", "busy", "table-a3", "table-45", "all-bits", "bad-bit" };

/// The flag byte (var_s0->unk0) of the object this run uses. The loadClut stub
/// may rewrite it, so the C's later read of it depends on the call.
var flag_byte: *u8 = undefined;

pub const stubs = struct {
    pub fn vs_main_loadClut(ctx: *zd.Ctx, img: anytype, slot: anytype, color_offset: anytype, count: anytype) void {
        _ = img;
        _ = slot;
        _ = color_offset;
        _ = count;
        if (ctx.value(u2) == 0) flag_byte.* = ctx.value(u8);
    }
};

fn flags(ctx: *zd.Ctx) u8 {
    return if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const table_a3 = v.g.D_800F4538();
    const table_45 = v.g.D_800F45E0();
    const arg0 = ctx.edge(c_int, &.{ 0, 1, 2, 7, 8, 14, 15, 16 });
    const idx: usize = @intCast(arg0);
    const arg1 = ctx.pick(c_int, &.{ 0, 1, 2, 3, -1, -2147483648 });
    const arg2 = ctx.pick(c_int, &.{ -1, -2, 0, 1, 6, 7, 8, 9, -2147483648, 2147483647 });

    // arg0 = 16 has no D_800F45E0 entry: the C reads one only when the
    // D_800F4538 entry is null, so arg0 = 16 always gets an object there.
    if (arg0 != 16 and ctx.chance(3)) {
        table_a3[idx] = null;
        if (ctx.chance(4)) {
            table_45[idx] = null;
        } else {
            const t = ctx.alloc(v.D_800F45E0_t);
            t.unk64.unk0 = flags(ctx);
            t.unkF = ctx.int(u8);
            table_45[idx] = t;
            flag_byte = &t.unk64.unk0;
        }
    } else {
        const obj = ctx.alloc(v.D_800F4538_t);
        obj.unk0.unk64.unk0 = flags(ctx);
        table_a3[idx] = obj;
        flag_byte = &obj.unk0.unk64.unk0;
    }

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    const a3 = v.g.D_800F4538()[idx];
    const t0 = if (a3 == null) v.g.D_800F45E0()[idx] else null;
    if (a3 == null and t0 == null) return 0;
    const f: u8 = if (a3) |o| o.unk0.unk64.unk0 else t0.?.unk64.unk0;
    if (args[2] == -1) return 4;
    if (@as(c_uint, @bitCast(args[2])) >= 8) return 5;
    if (f != 0) return 1;
    return if (a3 != null) 2 else 3;
}
