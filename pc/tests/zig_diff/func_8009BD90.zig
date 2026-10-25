//! zig_diff spec: func_8009BD90 -> func_8009BD90.func_8009BD90 (30DB0.c).
//! Inputs: a random vs_battle_objectData with index 0..16 (edges 0, 1, 2, 3,
//! 15, 16 often, so index == 2 makes `index >= 2` see equal sides) and modelId
//! across zero, positive, negative and 256/257 (the u8-truncation case). The
//! global table holds 17 entries, each null or one of four pooled objects
//! whose unk6E6 is mostly one shared key, so the match test fires often. The
//! pooled objects' unk5B1 takes 0, 1, 255 or the low byte of modelId, and
//! unk5D4 is null or an arena pointer. The C calls nothing external.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009BD90));

pub const classes = [_][:0]const u8{ "index-below-2", "model-zero", "model-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.vs_battle_objectData);
    const index: i8 = if (ctx.chance(2))
        ctx.edge(i8, &.{ 0, 1, 2, 3, 15, 16 })
    else
        ctx.range(i8, 0, 16);
    arg.index = index;
    arg.dataType = ctx.int(u8);
    arg.dataAddr = ctx.int(u32);
    arg.modelId = ctx.pick(i16, &.{ 0, 1, 2, 255, 256, 257, -1, -256, 0x7FFF, -32768 });
    if (ctx.chance(4)) arg.modelId = 0;

    const key = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
    const low: u8 = @truncate(@as(u16, @bitCast(arg.modelId)));
    var pool: [4]*v.D_800F4538_t = undefined;
    for (&pool) |*p| {
        const o = ctx.alloc(v.D_800F4538_t);
        o.unk6E6 = if (ctx.chance(4)) ctx.int(u16) else key;
        o.unk5B1 = ctx.pick(u8, &.{ 0, 1, 255, low });
        if (ctx.chance(2)) {
            psx.put(&o.unk5D4, @as(?*anyopaque, null));
        } else {
            psx.put(&o.unk5D4, @as(?*anyopaque, @ptrCast(ctx.alloc(u8))));
        }
        p.* = o;
    }

    // Every entry the C reads must be set: the index entry must be an object
    // (the C dereferences it), the others are null or a pooled object.
    const tbl = v.g.D_800F4538();
    const idx: usize = @intCast(index);
    for (0..17) |i| {
        if (i == idx or !ctx.chance(4)) {
            tbl[i] = pool[ctx.range(usize, 0, 3)];
        } else {
            tbl[i] = null;
        }
    }
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0].?;
    if (arg.index < 2) return 0;
    return if (arg.modelId == 0) 1 else 2;
}
