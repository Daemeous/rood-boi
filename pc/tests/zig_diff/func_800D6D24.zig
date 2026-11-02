//! zig_diff spec: func_800D6D24 -> func_800D6D24.func_800D6D24 (5BF94.c).
//! Inputs: a sequence whose frame counter unk6 is 0, 2 (it reaches 0), 3
//! or 5 (it does not); its command words (at byte offset unkE, unaligned
//! too) a run of offset commands (1) ending in a frame (0) or a stop (3),
//! then a restart (2) the cursor may start at; D_800F569C an arena P-file
//! whose four block 1 tables are arena blocks, the table index 0..3.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6D24));

pub const classes = [_][:0]const u8{ "counting", "frame", "stop", "restart" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const pf = ctx.alloc(v.D_800F569C_t);
    for (0..4) |i| psx.put(&pf.block1Tables[i], @as(*v.pFileBlock1Indices, @ptrCast(@alignCast(ctx.allocBytes(16, 16)))));
    v.g.D_800F569C().* = pf;
    const s = ctx.alloc(v.func_800D6CF_t);
    s.unk6 = ctx.pick(u8, &.{ 0, 0, 0, 2, 3, 5 });
    s.unk16 = ctx.range(u8, 0, 3);
    const misalign = ctx.pick(usize, &.{ 0, 0, 1, 2 }) % 4;
    const base = ctx.allocBytes(4 * 8 + 4, 16) + misalign;
    const n = ctx.range(usize, 0, 4);
    var k: usize = 0;
    while (k < n) : (k += 1) {
        const word: u32 = 0x10000000 | (ctx.int(u32) & 0x0FFFFFFF);
        @as(*align(1) u32, @ptrCast(base + 4 * k)).* = word;
    }
    const tail: u32 = if (ctx.random.boolean()) (ctx.int(u32) & 0x0FFFFFFF) else 0x30000000 | (ctx.int(u32) & 0x0FFFFFFF);
    @as(*align(1) u32, @ptrCast(base + 4 * n)).* = tail;
    @as(*align(1) u32, @ptrCast(base + 4 * (n + 1))).* = 0x20000000 | (ctx.int(u32) & 0x0FFFFFFF);
    psx.put(&s.unk18, @as(*u8, @ptrCast(base)));
    s.unkE = if (ctx.chance(3)) @intCast(4 * (n + 1)) else 0;
    return .{s};
}

pub fn classify(args: Args) usize {
    const s = args[0];
    if (s.*.unk6 != 0 and s.*.unk6 != 2) return 0;
    const base: [*]u8 = @ptrCast(psx.get(s.*.unk18).?);
    if (s.*.unkE != 0) return 3;
    var k: usize = 0;
    while (@as(*align(1) u32, @ptrCast(base + 4 * k)).* >> 28 == 1) k += 1;
    return if (@as(*align(1) u32, @ptrCast(base + 4 * k)).* >> 28 == 0) 1 else 2;
}
