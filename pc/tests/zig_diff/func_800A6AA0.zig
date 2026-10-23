//! zig_diff spec: func_800A6AA0 -> func_800A6AA0.func_800A6AA0 (3A1A0.c).
//! Inputs: a random actor in the arena whose animation id comes from edge
//! lists around every constant the C compares (0x13, 0x19, 0x2E..0x39, 0x5F,
//! 100 with its wrap), with the flag bits the C tests (0x8 and 0x8000 in
//! flags, 0x400000 in the word at +8) set or clear, unk1848 selecting the
//! quadrant, speed at 0, unk5B9, unk5B9-1, -1, small and large values, the
//! tick global at 0, +-1..3 and the extremes, and unk5B3 at 249/250/251.
//! func_800AD494 writes its out-parameter (NULL or an arena address) and
//! sometimes the actor's unk0.unkF; func_800A0204 sometimes rewrites the
//! tick global the C reads after it in the idle branch.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A6AA0));

pub const ignore_return = "the C's bare `return;` paths leave the int result undefined; its only caller (func_800A5280) ignores it";

pub const classes = [_][:0]const u8{ "flag8", "wide-table", "queued", "running", "walking", "idle" };

const anim_edges = [_]u8{
    0,    1,    6,    9,    0x0A, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x2D, 0x2E, 0x38,
    0x39, 0x3A, 0x5E, 0x5F, 0x63, 0x64, 0x65, 0x77, 0x78, 0xC3, 0xFF,
};

fn asActor(p: anytype) *v.D_800F4538_t {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);

    actor.animationId = if (ctx.chance(8))
        ctx.int(u8)
    else
        anim_edges[ctx.random.uintLessThan(usize, anim_edges.len)];

    psx.setBits(actor, "unk5AC_3", @intFromBool(ctx.chance(4)));
    psx.setBits(actor, "unk5AC_15", @intFromBool(ctx.chance(3)));
    const w = psx.byteAt(u32, actor, 8);
    w.* = if (ctx.chance(2)) (w.* | 0x400000) else (w.* & ~@as(u32, 0x400000));

    actor.unk1848.unk0 = ctx.pick(u8, &.{ 1, 1, 1, 6, 0 });
    // Quadrant 2 (unk6 - unk4 + 0x200 in 0x800..0xBFF) a third of the time: the
    // walk arms test it, and random headings reached it too rarely (the reviewer's change).
    if (ctx.chance(3)) actor.unk1848.unk6 = @bitCast(actor.unk1848.unk4 +% 0x800);

    actor.unk5B9 = ctx.pick(u8, &.{ 0, 1, 2, 3, 100, 255 });
    const speed: c_int = switch (ctx.int(u8) % 8) {
        0 => 0,
        1 => @as(c_int, actor.unk5B9),
        2 => @as(c_int, actor.unk5B9) - 1,
        3 => -1,
        4 => ctx.range(c_int, -3, 3),
        else => ctx.range(c_int, -300, 300),
    };

    v.g.vs_gametime_tickspeed().* = ctx.pick(i32, &.{ 0, 1, 2, 3, -1, -2, -3, 0x7FFF_FFFF, -0x8000_0000 });
    actor.unk5B3 = ctx.pick(u8, &.{ 0, 1, 124, 249, 250, 251, 255 });

    return .{ actor, speed, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    const speed = args[1];
    if ((psx.byteAt(u32, actor, 0x5AC).* & 8) != 0) return 0;
    if ((psx.byteAt(u32, actor, 8).* & 0x400000) != 0) return 1;
    if ((psx.byteAt(u32, actor, 0x5AC).* & 0x8000) != 0) return 2;
    if (speed >= @as(c_int, actor.unk5B9) - 1) return 3;
    if (speed > 0) return 4;
    return 5;
}

pub const stubs = struct {
    pub fn func_800A0204(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        if (ctx.value(u8) < 64) v.g.vs_gametime_tickspeed().* = ctx.value(i32);
        return ctx.value(c_int);
    }

    pub fn func_800AD494(ctx: *zd.Ctx, actor: anytype, next: anytype, entry: anytype) c_int {
        _ = next;
        const a = asActor(actor);
        if (ctx.value(u8) < 64) a.unk0.unkF = ctx.value(u8);
        const bytes: *[@sizeOf(usize)]u8 = @ptrCast(entry);
        const p: usize = if (ctx.value(u8) < 128) 0 else @intFromPtr(ctx.alloc(u16));
        std.mem.writeInt(usize, bytes, p, .little);
        return ctx.value(c_int);
    }
};
