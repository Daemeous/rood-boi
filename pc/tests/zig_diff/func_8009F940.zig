//! zig_diff spec: func_8009F940 -> func_8009F940.func_8009F940 (30DB0.c).
//! Inputs: an index 0-16 into D_800F4538 (its entry is null a quarter of the
//! time, otherwise a random-filled D_800F4538_t in the arena), arg1 from
//! {0, 1, -1, 256, INT_MIN} or any int (so the u8 store truncates and the sign
//! bit is set or clear), and arg2/arg3 as arena blocks, or null when arg1 is 0
//! (the C dereferences them only when arg1 != 0). Some arg3 values alias the
//! object's unk6F4 field, so the order of the two copies is observable.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F940));

pub const classes = [_][:0]const u8{ "null-entry", "arg1-zero", "store", "alias-arg3" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const table = v.g.D_800F4538();
    for (table) |*e| e.* = obj;
    const idx = ctx.range(c_int, 0, 16);
    if (ctx.chance(4)) table[@intCast(idx)] = null;

    const arg1 = ctx.pick(c_int, &.{ 0, 1, -1, 256, std.math.minInt(c_int) });

    var arg2: ?*v.D_800F4538_unk58_2 = null;
    var arg3: ?*v.D_800F4538_unk58_2 = null;
    if (arg1 != 0 or ctx.chance(2)) arg2 = ctx.alloc(v.D_800F4538_unk58_2);
    if (arg1 != 0 or ctx.chance(2)) {
        if (ctx.chance(3)) {
            const alias: *v.D_800F4538_unk58_2 = @ptrCast(&obj.unk6F4);
            arg3 = alias;
        } else {
            arg3 = ctx.alloc(v.D_800F4538_unk58_2);
        }
    }
    return .{ idx, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const obj = table[@intCast(args[0])] orelse return 0;
    if (args[1] == 0) return 1;
    if (args[3]) |p| {
        if (@intFromPtr(p) == @intFromPtr(&obj.unk6F4)) return 3;
    }
    return 2;
}
