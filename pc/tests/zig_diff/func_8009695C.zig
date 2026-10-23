//! zig_diff spec: func_8009695C -> func_8009695C.func_8009695C (2842C.c).
//! Inputs: x and y near the i16 and int16 limits or in a moderate range (never
//! enough to overflow int), and a D_800F1BAC_t in the arena with random bytes
//! after its fields, so displayTextLen above 4 reads past displayText as the C
//! does. unkE and unkA index within the two tables (7 and 13 entries). unkC is
//! random, so its sign bit is set and clear. The scratchpad's OT and prim
//! pointers are arena blocks. AddPrim is a stub: the C never reads back what it
//! links into the table.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009695C));

pub const classes = [_][:0]const u8{ "no-text", "text-positive", "text-negative", "long-text" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: *v.vs_scratch_t = @ptrFromInt(zd.scratchpad);
    const prims = ctx.allocBytes(12288, 16);
    const ot = ctx.allocBytes(64, 16);
    psx.put(&scratch.unk0, prims);
    psx.put(&scratch.unk4, ot);

    const p: *v.D_800F1BAC_t = @ptrCast(@alignCast(ctx.allocBytes(512, 16)));
    p.unk12 = ctx.int(i16);
    p.unkC = ctx.int(u16);
    p.unkE = ctx.range(u8, 0, 6);
    p.unkA = ctx.range(u8, 0, 12);
    p.displayTextLen = if (ctx.chance(2))
        ctx.range(u8, 0, 255)
    else
        ctx.edge(u8, &.{ 0, 1, 2, 4, 5, 255 });

    const x: c_int = if (ctx.chance(4))
        ctx.edge(c_int, &.{ 0, -1, 32767, -32768, 65535, -65536 })
    else
        ctx.range(c_int, -40000, 40000);
    const y: c_int = if (ctx.chance(4))
        ctx.edge(c_int, &.{ 0, -1, 32767, -32768, 65535, -65536 })
    else
        ctx.range(c_int, -40000, 40000);
    return .{ x, y, p };
}

pub fn classify(args: Args) usize {
    const p = args[2].?;
    if (p.displayTextLen == 0) return 0;
    if (p.displayTextLen > 4) return 3;
    return if ((p.unkC & 0x8000) != 0) 2 else 1;
}

pub const stubs = struct {
    /// AddPrim links the prim into the ordering table; the C never reads the
    /// table or the prim back, so the stub changes nothing the C sees.
    pub fn AddPrim(ctx: *zd.Ctx, ot: anytype, prim: anytype) void {
        _ = ctx;
        _ = ot;
        _ = prim;
    }
};
