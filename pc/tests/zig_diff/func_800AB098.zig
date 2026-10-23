//! zig_diff spec: func_800AB098 -> func_800AB098.func_800AB098 (40564.c).
//! Inputs: an actor in the arena (padded on both sides so negative and large
//! indices stay in memory the harness owns), the unit's 0xFF-hidden byte at
//! any of unk183E[0..7], an index from -1000..1000 (edges 0, 1, -1 included),
//! a frame of any sign near zero or within +-2^20, the bit 27 of 0x5AC set or
//! clear, the anim entry at index either drawable (columns 1, 2, 3 or 255,
//! any overlay 0..255 so overlay-1 reaches -1) or empty, and the fallback
//! columns zero or 1..255. The slot is 0..23 so the 4-byte-stride tables stay
//! inside their 96-byte gaps. The scratchpad packet slot holds a valid arena
//! block. The packet stubs write the rect and the x/y arguments into the
//! packet, so a wrong rect or a dropped call shows in the state.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AB098));

pub const classes = [_][:0]const u8{ "hidden", "fallback-empty", "fallback-used", "anim-narrow", "anim-wide" };

var run_class: usize = 0;

pub const stubs = struct {
    pub fn SetDrawMove(ctx: *zd.Ctx, p: anytype, rect: anytype, x: c_int, y: c_int) void {
        const d: *v.DR_MOVE = @ptrCast(@alignCast(p));
        const r: *v.RECT = @ptrCast(@alignCast(rect));
        d.tag = @bitCast(y);
        d.code[0] = @as(u16, @bitCast(r.x));
        d.code[1] = @as(u16, @bitCast(r.y));
        d.code[2] = @as(u16, @bitCast(r.w));
        d.code[3] = @as(u16, @bitCast(r.h));
        d.code[4] = @bitCast(x);
        if (ctx.value(u1) == 1) d.code[0] = ctx.value(u32);
    }

    pub fn func_8007A824(ctx: *zd.Ctx, p: anytype) void {
        const d: *v.DR_MOVE = @ptrCast(@alignCast(p));
        d.tag = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // Room below the actor for negative indices, and after it for large ones.
    _ = ctx.allocBytes(8192, 16);
    const actor = ctx.alloc(v.D_800F4538_t);
    _ = ctx.allocBytes(8192, 16);
    const packet = ctx.alloc(v.DR_MOVE);
    @as(*align(1) u32, @ptrFromInt(zd.scratchpad)).* = psx.addr(packet);

    const sel = ctx.range(u8, 0, 7);
    v.g.D_800E8F2C().* = sel;
    actor.unk5BB = ctx.range(u8, 0, 23);

    const hidden = ctx.chance(6);
    psx.byteAt(u8, actor, 0x183E + @as(isize, sel)).* =
        if (hidden) 0xFF else ctx.edge(u8, &.{ 0, 1, 7, 0x7F, 0x80, 0xFE });

    const index: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 1, 2, 3, 7, 8, 9, -1, -2 })
    else
        ctx.range(c_int, -1000, 1000);
    const frame: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 1, -1, 2, -2, 7, -7, 8, 15, 16, 100, -100 })
    else
        ctx.range(c_int, -(1 << 20), 1 << 20);

    const wide = ctx.chance(2);
    psx.setBits(actor, "unk5AC_27", @as(u1, @intFromBool(wide)));

    const anim = psx.byteAt(v.func_800AB098_t, actor, 0x1804 + @as(isize, index) * 4);
    const fallback_columns = psx.byteAt(u8, actor, 0x614 + @as(isize, index) * 4 + 2);
    var cls: usize = 0;
    if (hidden) {
        cls = 0;
    } else if (ctx.chance(3)) {
        anim.columns = 0;
        if (ctx.chance(3)) {
            fallback_columns.* = 0;
            cls = 1;
        } else {
            fallback_columns.* = ctx.range(u8, 1, 255);
            cls = 2;
        }
    } else {
        anim.columns = ctx.edge(u8, &.{ 1, 2, 3, 255 });
        cls = if (wide) 4 else 3;
    }
    run_class = cls;
    return .{ actor, index, frame };
}

pub fn classify(args: Args) usize {
    _ = args;
    return run_class;
}
