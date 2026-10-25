//! zig_diff spec: func_8009E634 -> func_8009E634.func_8009E634 (30DB0.c).
//! Inputs: one D_800F4538_t in the arena with a random fill. unkF is zero or
//! not (the whole body is skipped otherwise). animationId comes from edge
//! values around the three windows the C tests (0x47-0x4A, 0x4C, 0x4F-0x52)
//! and sometimes any byte, so the `- 0x4F >= 4U` comparison sees equal sides
//! (0x53) and the inner `- 0x47 >= 4U` sees them at 0x4B. facing comes from
//! edges around the quadrant boundaries (+0x200 and &0xFFF, so 1536 and 2047
//! give temp == 2, the `< 2` edge) and sometimes any i16, so negative and
//! wrapping facings occur. currentTileX/Z sit at 0, 1, 254 and 255 so the
//! tile sums cross 255. The callee func_800E4C28 is a void recording stub:
//! the C never reads anything it could change after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E634));

pub const classes = [_][:0]const u8{ "flag-set", "anim-out", "anim-hi", "anim-lo" };

const anim_edges = [_]u8{ 0x00, 0x46, 0x47, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x52, 0x53, 0x54, 0xFF };
const facing_edges = [_]i16{ 0, -512, 511, 512, 1535, 1536, 2047, -1, 32767, -32768, -2048, -1024 };
const tile_edges = [_]u8{ 0, 1, 254, 255 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F4538_t);

    // unkF == 0 most of the time; the non-zero values skip the whole body.
    arg.unk0.unkF = if (ctx.chance(5)) ctx.edge(u8, &.{ 1, 0x80, 0xFF }) else 0;

    arg.animationId = if (ctx.chance(4)) ctx.int(u8) else ctx.edge(u8, &anim_edges);

    arg.unk0.facing = if (ctx.chance(3)) ctx.int(i16) else ctx.edge(i16, &facing_edges);

    arg.unk0.currentTileX = if (ctx.chance(3)) ctx.int(u8) else ctx.edge(u8, &tile_edges);
    arg.unk0.currentTileZ = if (ctx.chance(3)) ctx.int(u8) else ctx.edge(u8, &tile_edges);

    return .{arg};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    if (a.unk0.unkF != 0) return 0;
    const anim = a.animationId;
    if ((anim -% 0x4F) < 4) return 2;
    if (((anim -% 0x47) < 4) or anim == 0x4C) return 3;
    return 1;
}
