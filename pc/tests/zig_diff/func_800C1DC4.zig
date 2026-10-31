//! zig_diff spec: func_800C1DC4 -> func_800C1DC4.func_800C1DC4 (58578.c).
//! Inputs: one row record in the arena (selected or idle, gradient 0..8 with
//! edges and the sign bit, age around 18, x around 160 including negatives,
//! any bank, random SVECTOR bytes), its text pointer and the scratch
//! pointers (prim list, next prim, ordering base) into one arena block.
//! The row's selected/left/aged combinations give the five classes. The
//! stubs model what the real callees can change: the text draw rewrites the
//! tag word at nextPrim (the C reads it back), the ring draws may rewrite the
//! row's age (the C re-tests it), and the GTE-side step may rewrite the row's
//! x (the C re-tests it after the projection). The pc_classic_site hook is
//! always linked as a recording stub, so its null side cannot be reached
//! (see the coverage_waiver entries in specs.toml).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_58578");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C1DC4));

pub const classes = [_][:0]const u8{
    "idle",
    "selected-left",
    "selected-right",
    "selected-left-aged",
    "selected-right-aged",
};

/// The row prepare built, for the stubs to write back into.
var row: ?*v.D_800EB9B8_unk990 = null;

pub const stubs = struct {
    /// The text draw writes the tag word of nextPrim, which the C reads back.
    pub fn vs_battle_renderTextRawColor(ctx: *zd.Ctx, text: anytype, xy: anytype, color: anytype, nextPrim: anytype) void {
        _ = text;
        _ = xy;
        _ = color;
        const raw: ?*anyopaque = nextPrim;
        const word: *u32 = @ptrCast(@alignCast(raw.?));
        word.* = ctx.value(u32);
    }

    /// The GTE-side step may move the row: the C re-tests x after it.
    pub fn func_800C02A8(ctx: *zd.Ctx) void {
        const r = row.?;
        if (ctx.value(u1) == 1) r.unk4 = ctx.value(i16);
    }

    /// A ring draw may change the row's age: the C re-tests it after the call.
    pub fn func_800C1A40(ctx: *zd.Ctx, sxy: anytype, xy: anytype, age: anytype) void {
        _ = sxy;
        _ = xy;
        _ = age;
        const r = row.?;
        if (ctx.value(u1) == 1) r.unk3 = ctx.value(u8);
    }

    pub fn func_800C16FC(ctx: *zd.Ctx, sxy: anytype, xy: anytype, age: anytype) void {
        _ = sxy;
        _ = xy;
        _ = age;
        const r = row.?;
        if (ctx.value(u1) == 1) r.unk3 = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const r = ctx.alloc(v.D_800EB9B8_unk990);
    row = r;
    r.unk1 = ctx.pick(u8, &.{ 0, 1, 0x80, 255 });
    r.unk2 = ctx.pick(u8, &.{ 0, 1, 7, 8, 9, 0x80, 255 });
    r.unk3 = ctx.pick(u8, &.{ 0, 17, 18, 19, 31, 255 });
    r.unk4 = ctx.pick(i16, &.{ 0, 159, 160, 161, -1, 32767, -32768 });
    r.unk6 = ctx.int(u8);
    r.unk7 = ctx.pick(u8, &.{ 0, 1, 31, 255 });

    // One block for the scratchpad-side buffers: the prim list at +256, the
    // text at +512, the source of the scratch's slot pointer at +1024, and the
    // ordering base at +8192 (nextPrim steps back at most 4048 bytes from it).
    const blk = ctx.allocBytes(12288, 16);
    const s = v.g.vs_scratch();
    psx.put(&s.unk0, @as(*anyopaque, @ptrCast(blk + 256)));
    psx.put(&s.unk4, @as(*anyopaque, @ptrCast(blk + 1024)));
    psx.put(&s.unk8, @as(*anyopaque, @ptrCast(blk + 8192)));
    psx.put(&r.unk10, @as(*u8, @ptrCast(blk + 512)));

    v.g.pc_gte_anchored().* = ctx.range(i32, 0, 3);
    v.g.D_800F4CB4().* = ctx.int(u8);

    return .{r};
}

pub fn classify(args: Args) usize {
    const r = args[0].?;
    if (r.unk1 == 0) return 0;
    const left = r.unk4 < 160;
    const aged = r.unk3 >= 18;
    if (left) return if (aged) 3 else 1;
    return if (aged) 4 else 2;
}
