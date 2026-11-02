//! zig_diff spec: func_800BB874 -> func_800BB874.func_800BB874 (4A0A8.c).
//! Inputs: the scratchpad holds a guest pointer to a 64-line buffer in the
//! arena (the OT pointer at +8 is usually a valid arena object, sometimes a
//! random word the stubs never dereference). D_800F4B20 is 0, 1, or one of the
//! two values whose increment wraps or sits at the wrap (0xFFFE, 0xFFFF), so
//! both parities of (i + (D >> 1)) are seen. The loop count is a small edge
//! (0 to 40) so the buffer is never overrun. D_8005DFD6 is a signed edge
//! (-1, 0, 1, 32767, -32768). The stubs write the LINE_F2 tag and code that
//! the C reads back after SetLineF2, and the DR_TPAGE and tag words that
//! SetDrawTPage and AddPrim write.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB874));

pub const classes = [_][:0]const u8{ "no-lines", "wrap", "lines" };

const max_lines = 64;

pub const stubs = struct {
    pub fn SetLineF2(ctx: *zd.Ctx, p: anytype) void {
        // The real SetLineF2 writes the tag and the code byte; the C ORs a bit into the code.
        const l: *v.LINE_F2 = @ptrCast(@alignCast(p));
        l.tag = ctx.value(u32);
        l.code = ctx.value(u8);
    }

    pub fn AddPrim(ctx: *zd.Ctx, ot: anytype, p: anytype) void {
        _ = ot;
        // The real AddPrim links the primitive: its first word changes.
        const l: *v.LINE_F2 = @ptrCast(@alignCast(p.?));
        l.tag = ctx.value(u32);
    }

    pub fn SetDrawTPage(ctx: *zd.Ctx, p: anytype, dfe: anytype, dtd: anytype, tpage: anytype) void {
        _ = dfe;
        _ = dtd;
        _ = tpage;
        const t: *v.DR_TPAGE = @ptrCast(@alignCast(p));
        t.tag = ctx.value(u32);
        t.code[0] = ctx.value(u32);
    }

    pub fn GetTPage(ctx: *zd.Ctx, tp: anytype, abr: anytype, x: anytype, y: anytype) u16 {
        _ = tp;
        _ = abr;
        _ = x;
        _ = y;
        return ctx.value(u16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const lines: [*]v.LINE_F2 = @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.LINE_F2) * max_lines, 16)));
    const ot = ctx.alloc(u32);
    @as(*align(1) u32, @ptrFromInt(zd.scratchpad)).* = psx.addr(&lines[0]);
    @as(*align(1) u32, @ptrFromInt(zd.scratchpad + 8)).* = if (ctx.chance(8)) ctx.int(u32) else psx.addr(ot);
    v.g.D_800F4B20().* = ctx.pick(u16, &.{ 0, 1, 0xFFFE, 0xFFFF });
    v.g.D_8005DFDA().* = ctx.edge(u16, &.{ 0, 1, 2, 3, 5, 9, 16, 40 });
    v.g.D_8005DFD6().* = ctx.pick(i16, &.{ -1, 0, 1, 32767, -32768 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_8005DFDA().* == 0) return 0;
    const d = v.g.D_800F4B20().*;
    if (d >= 0xFFFE) return 1;
    return 2;
}
