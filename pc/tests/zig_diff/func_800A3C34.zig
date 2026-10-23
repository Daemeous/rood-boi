//! zig_diff spec: func_800A3C34 -> func_800A3C34.func_800A3C34 (3A1A0.c).
//! The C makes no calls, so there are no stubs. Inputs: four arguments (tile
//! coordinates 0..255 and edges, arg2 near the actors' vy so `>=` sees equal
//! sides and one-off neighbours, arg3 across 0..18 with its edges 1, 2, 16, 17),
//! and the two tables the C reads. The 17-slot actor table is filled with null or
//! one of three shared arena actors whose skip, table index, tile, position and
//! bitfield values are randomised around the arguments. The slot-0 test word
//! (the four bytes before unk5B4) takes bit-pattern edges around 0x0FFFFFFF and
//! 0x10000000, so the masked compare sees equal sides often. The 16-slot
//! D_800F45E0 table points at two shared arena blocks whose unk5C/unk5E sometimes
//! match the arguments.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3C34));

pub const classes = [_][:0]const u8{ "arg3-past-table", "arg3-one", "arg3-below-two", "arg3-scan" };

fn edge(ctx: *zd.Ctx, comptime T: type, edges: []const T) T {
    return edges[ctx.range(usize, 0, edges.len - 1)];
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0: c_int = edge(ctx, c_int, &.{ 0, 1, 2, 127, 128, 255, 256, -1 });
    const a1: c_int = edge(ctx, c_int, &.{ 0, 1, 2, 127, 128, 255, 256, -1 });
    const a2: c_int = if (ctx.chance(3))
        edge(ctx, c_int, &.{ 0, 191, 192, 193, 32511, 32512, -32768, -40000 })
    else
        ctx.range(c_int, -40000, 40000);
    const a3: c_int = edge(ctx, c_int, &.{ -3, -1, 0, 1, 2, 3, 15, 16, 17, 18, 100 });

    const word_edges = [_]u32{ 0x0FFFFFFF, 0x10000000, 0xF0000000, 0x00000000, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000 };

    // Shared actors: the C only reads them, so a few serve all 17 slots.
    var pool: [3]*v.D_800F4538_t = undefined;
    for (&pool) |*p| p.* = ctx.alloc(v.D_800F4538_t);
    for (pool) |a| {
        psx.setBits(&a.unk0, "skip", @intFromBool(ctx.chance(5)));
        a.unk6E6 = if (ctx.chance(6)) 0x7F else ctx.int(u16);
        psx.setBits(&a.unk0, "unkB_4", ctx.range(u3, 0, 7));
        psx.setBits(&a.unk0, "unkA_0", if (ctx.chance(2)) 0 else ctx.range(u3, 0, 7));
        psx.setBits(&a.unk0, "unkB_0", if (ctx.chance(2)) 0 else ctx.range(u4, 0, 15));
        psx.setBits(&a.unk0, "unkA_3", ctx.range(u2, 0, 3));
        psx.setBits(&a.unk0, "unkA_5", @intFromBool(ctx.chance(2)));
        a.unk0.currentTileX = if (ctx.chance(3)) psx.conv(u8, a0) else ctx.int(u8);
        a.unk0.currentTileZ = if (ctx.chance(3)) psx.conv(u8, a1) else ctx.int(u8);
        // Exactly at arg2 - 192, or one below/above it, so `>=` against the
        // unadjusted or mis-adjusted arg2 gives different answers.
        a.unk0.position.vy = if (ctx.chance(3))
            psx.conv(i16, a2 - 192 + edge(ctx, c_int, &.{ -1, 0, 0, 1 }))
        else
            ctx.int(i16);
        a.unk5EC.vx = if (ctx.chance(2)) psx.conv(i16, a0) else ctx.int(i16);
        a.unk5EC.vy = if (ctx.chance(2)) psx.conv(i16, a1) else ctx.int(i16);
        // The word before unk5B4: its top nibble is unk5AC_28, its top bit and
        // the 0x0FFFFFFF/0x10000000 boundary drive the slot-0 test. Written last
        // so the bitfield writes above do not overwrite it.
        const w: u32 = if (ctx.chance(2))
            word_edges[ctx.range(usize, 0, word_edges.len - 1)]
        else
            ctx.int(u32);
        std.mem.writeInt(u32, a._bits_1452[0..4], w, .little);
    }

    const table = v.g.D_800F4538();
    for (table) |*slot| {
        slot.* = if (ctx.chance(5)) null else pool[ctx.range(usize, 0, pool.len - 1)];
    }

    var tmp: [2]*v.D_800F45E0_t = undefined;
    for (&tmp) |*t| {
        t.* = ctx.alloc(v.D_800F45E0_t);
        t.*.unk5C = if (ctx.chance(3)) psx.conv(u8, a0) else ctx.int(u8);
        t.*.unk5E = if (ctx.chance(3)) psx.conv(u8, a1) else ctx.int(u8);
    }
    const t45 = v.g.D_800F45E0();
    for (t45) |*slot| slot.* = tmp[ctx.range(usize, 0, tmp.len - 1)];

    return .{ a0, a1, a2, a3 };
}

pub fn classify(args: Args) usize {
    const a3 = args[3];
    if (a3 >= 17) return 0;
    if (a3 == 1) return 1;
    if (a3 < 2) return 2;
    return 3;
}
