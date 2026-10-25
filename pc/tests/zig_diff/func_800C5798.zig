//! zig_diff spec: func_800C5798 -> func_800C5798.func_800C5798 (5BF94.c).
//! Inputs: a D_800EB9B8 object in the arena whose unk990 table takes arg0 in
//! 0-23, an arg1 that is 2 or another value (the limb branch), an arg2 in a
//! range where arg2 * 11 does not overflow, and a src array whose entry for
//! arg0 has unk8 in 0-19, the entries of vs_battle_limbNames that prepare
//! sets to arena blocks. vs_battle_rMemzero zeroes its block, as the real
//! callee does.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C5798));

pub const classes = [_][:0]const u8{ "limb-2", "limb-other" };

pub const stubs = struct {
    pub fn vs_battle_rMemzero(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = ctx;
        const q: ?*anyopaque = p;
        const bytes: [*]u8 = @ptrCast(q.?);
        const len: usize = @intCast(n);
        @memset(bytes[0..len], 0);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800C56C0_t);
    v.g.D_800EB9B8().* = obj;

    const srcs: [*]v.func_800C5798_t = @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.func_800C5798_t) * 24, 16)));
    psx.put(&obj.unk40, &srcs[0]);

    // Every entry of the 20-slot table gets a valid arena block.
    for (0..20) |i| v.g.vs_battle_limbNames()[i] = &ctx.allocBytes(16, 16)[0];

    const arg0 = ctx.range(c_int, 0, 23);
    const arg1 = ctx.edge(c_int, &.{ 0, 1, 2, 3, -1 });
    const arg2 = ctx.range(c_int, -100_000_000, 100_000_000);

    // The limb index the C reads: inside the 20-slot table.
    srcs[@intCast(arg0)].unk8 = ctx.range(u8, 0, 19);

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    return if (args[1] == 2) 0 else 1;
}
