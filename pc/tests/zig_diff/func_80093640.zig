//! zig_diff spec: func_80093640 -> func_80093640.func_80093640 (2842C.c).
//! Inputs: arg0 is null or an arena object (the C never reads it); arg1 carries
//! an index s32 in -64..31 (slot 0..95 of D_800F1DD8, the table's edges
//! included); the chosen slot's unk0 is idle (0) or nonzero (1, 0x80, 0xFF, or
//! random); its unk4 block is null or an arena object (the null case reaches
//! freeHeap with a null argument); unk8 is an arena object. Classes: idle,
//! release (freeHeap, copy, clear), and release with a null block.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093640));

pub const classes = [_][:0]const u8{ "idle", "release", "release-null" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: ?*v.func_80092F74_t = if (ctx.chance(2)) null else ctx.alloc(v.func_80092F74_t);
    const arg1 = ctx.alloc(v.func_80092F74_t2);

    const idx: i32 = if (ctx.chance(2))
        ctx.edge(i32, &.{ -64, -63, -1, 0, 1, 30, 31 })
    else
        ctx.range(i32, -64, 31);
    arg1.unk4[0].s32 = idx;

    const slot: usize = @intCast(idx + 64);
    const tbl = v.g.D_800F1DD8();
    const e = &tbl[slot];
    e.unk0 = if (ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
    const blk: ?*v.D_800F1DD8_t2 = if (ctx.chance(6)) null else ctx.alloc(v.D_800F1DD8_t2);
    psx.put(&e.unk4, blk);
    psx.put(&e.unk8, ctx.alloc(v.D_800F1DD8_t2));

    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const arg1 = args[1].?;
    const slot: usize = @intCast(arg1.unk4[0].s32 + 64);
    const e = &v.g.D_800F1DD8()[slot];
    if (e.unk0 == 0) return 0;
    if (e.unk4.raw == 0) return 2;
    return 1;
}

pub const stubs = struct {
    /// Frees a block the C hands it: sometimes the block's first word changes.
    pub fn vs_main_freeHeap(ctx: *zd.Ctx, p: anytype) void {
        const q: ?*anyopaque = p;
        if (q) |block| {
            if (ctx.value(u1) == 1) {
                const w: *align(1) u32 = @ptrCast(block);
                w.* = ctx.value(u32);
            }
        }
    }
};
