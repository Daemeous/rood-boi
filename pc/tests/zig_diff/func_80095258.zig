//! zig_diff spec: func_80095258 -> func_80095258.func_80095258 (2842C.c).
//! Inputs: a D_800F1BAC_t with unkB 0-30 (every step branch, with the edges
//! 4, 6, 15, 19, 25 and 27 picked often), unkE random (its low bit picks the
//! slide and the panel), and unkC 0-31 selecting a D_800690B4 entry with 0,
//! 1 or 2 labels into D_80068FAC (random rects, widths around 0 so x + w hits
//! 0), color 0-3 into D_80069134, and x around 0-320 (sometimes any i16) and y
//! around 0. The scratchpad holds a prim buffer and an ordering table in the
//! arena. func_800957D0 may move the prim buffer pointer, and AddPrim writes
//! the ordering table and the prim's tag, so both are read back by the C.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80095258));

pub const classes = [_][:0]const u8{
    "count-zero",
    "slide-in unkB<4",
    "unkB 4-5",
    "unkB 6-24",
    "unkB 25-26",
    "unkB 27-30",
};

pub const stubs = struct {
    pub fn func_800957D0(ctx: *zd.Ctx, x: anytype, y: anytype, width: anytype) void {
        _ = x;
        _ = y;
        _ = width;
        // The panel draw may hand back a new prim buffer: the C reads the
        // scratchpad pointer again after the call.
        const scratch: *v.vs_scratch_t = @ptrFromInt(zd.scratchpad);
        if (ctx.value(u1) == 1) {
            psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(512, 16))));
        }
    }

    pub fn AddPrim(ctx: *zd.Ctx, ot: anytype, p: anytype) void {
        // The ordering table links the prim in, and the prim's tag word changes.
        const word: *u32 = @ptrCast(@alignCast(ot.?));
        word.* = ctx.value(u32);
        const prim: *v.POLY_FT4 = @ptrCast(@alignCast(p.?));
        prim.tag = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p = ctx.alloc(v.D_800F1BAC_t);
    const idx = ctx.range(u16, 0, 31);
    p.unkC = idx;
    p.unkB = if (ctx.chance(2))
        ctx.edge(u8, &.{ 0, 3, 4, 5, 6, 14, 15, 16, 19, 20, 24, 25, 26, 27, 28, 29, 30 })
    else
        ctx.range(u8, 0, 30);
    p.unkE = ctx.int(u8);

    const table = v.g.D_800690B4();
    table[idx].count = if (ctx.chance(6)) 0 else ctx.range(u8, 1, 2);
    table[idx].labels = .{ ctx.range(u8, 0, 32), ctx.range(u8, 0, 32) };
    table[idx].color = ctx.range(u8, 0, 3);

    const rects = v.g.D_80068FAC();
    var k: usize = 0;
    while (k < 33) : (k += 1) {
        rects[k].x = ctx.int(i16);
        rects[k].y = ctx.int(i16);
        rects[k].w = ctx.range(i16, -20, 60);
        rects[k].h = ctx.int(i16);
    }

    const scratch: *v.vs_scratch_t = @ptrFromInt(zd.scratchpad);
    const cursor = ctx.allocBytes(512, 16);
    const ot = ctx.allocBytes(64, 16);
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(cursor)));
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ot + 20)));

    const x: c_int = if (ctx.chance(4))
        @as(c_int, ctx.int(i16))
    else
        ctx.range(c_int, -80, 400);
    const y: c_int = ctx.range(c_int, -100, 300);
    return .{ x, y, p };
}

pub fn classify(args: Args) usize {
    const p = args[2].?;
    if (v.g.D_800690B4()[p.unkC].count == 0) return 0;
    const b = p.unkB;
    if (b < 4) return 1;
    if (b < 6) return 2;
    if (b >= 27) return 5;
    if (b >= 25) return 4;
    return 3;
}
