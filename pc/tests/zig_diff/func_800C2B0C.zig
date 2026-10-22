//! zig_diff spec: func_800C2B0C -> func_800C2B0C.func_800C2B0C (58578.c).
//! Inputs: a position whose tile x and z sit around a map of 0-20 by -2..20
//! tiles (sometimes random 16-bit words, so the signed `>> 7` and the
//! out-of-map tests see negative and huge values), a row 0-8 (or an edge:
//! 9, 10, 0xFFFFFFFF, 0x80000000), the room-section block D_800EB9B8 in the
//! arena, and the D_800F45E0 table with all 16 entries pointing at one
//! object whose unk6C[8].actorId is 0-7. Each run makes its rooms one of
//! three kinds: solid (bit 1 of unk0_5 set), skipped (unk0_10 in 5-7 and bit
//! 1 of unk0_18 clear), or scanned (unk0_10 < 5, or bit 1 of unk0_18 set).
//! Half the runs make every tile height in the scan the same value, so the
//! `low == high` compare sees equal operands (the flat mode), and the
//! other half give random small or 32-bit heights. The stubs return the map
//! dimensions from a prepared word, a room drawn from nine prepared rooms,
//! a callee out-parameter that is 0 or any i16 (its actorId gate picks the
//! offset path), and tile heights as described above.
//! Note: `value != 0` only ever tests 0x10000 or 0, so no input gives it a
//! set sign bit; that is the C's own structure, not a gap in the inputs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C2B0C));

pub const classes = [_][:0]const u8{ "row-out", "off-map", "solid", "skipped", "scanned" };

/// The dimensions word _getCollisionMapDimensions returns (set by prepare).
var g_dims: c_int = 0;
/// 0: rooms are solid, 1: rooms are skipped, 2: rooms are scanned.
var g_mode: u8 = 0;
/// The nine rooms func_8008B764 hands out (allocated by prepare).
var rooms: [9]*v._mpdRoomSection3 = undefined;
/// Flat mode: every tile height is g_flat_val, so the scan's low and high match.
var g_flat: bool = false;
var g_flat_val: i32 = 0;

pub const stubs = struct {
    pub fn _getCollisionMapDimensions(ctx: *zd.Ctx, arg0: anytype) c_int {
        _ = ctx;
        _ = arg0;
        return g_dims;
    }

    pub fn func_8008B764(ctx: *zd.Ctx, x: anytype, z: anytype, arg2: anytype) ?*v._mpdRoomSection3 {
        _ = x;
        _ = z;
        _ = arg2;
        return rooms[ctx.value(u8) % 9];
    }

    /// The offset out-parameter: 0 a quarter of the time, else any i16.
    pub fn func_800FA188(ctx: *zd.Ctx, x: anytype, z: anytype, offset: anytype) c_int {
        _ = x;
        _ = z;
        offset.* = if (ctx.value(u2) == 0) 0 else @as(c_int, ctx.value(i16));
        return ctx.value(i32);
    }

    /// An index into D_800F45E0 (0-15).
    pub fn func_800A91DC(ctx: *zd.Ctx, a: anytype, b: anytype, c2: anytype) c_int {
        _ = a;
        _ = b;
        _ = c2;
        return @as(c_int, ctx.value(u8) & 15);
    }

    /// A tile height: in flat mode always g_flat_val; otherwise often one of
    /// four small values (so comparisons repeat), else any 32-bit word.
    pub fn func_8008DDA8(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        if (g_flat) return g_flat_val;
        const r = ctx.value(u8);
        if (r < 160) return @as(c_int, r & 3);
        return ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // D_800EB9B8: the block the C writes (unk4AB8, unk4370).
    v.g.D_800EB9B8().* = ctx.alloc(v.D_800EB9B8_t);

    // D_800F45E0: all 16 entries point at one object; unk6C[8].actorId is 0-7,
    // mostly 2-7 so the offset gate does not zero the value.
    const fx = ctx.alloc(v.D_800F45E0_t);
    const actor_id: u3 = if (ctx.chance(4)) ctx.range(u3, 0, 1) else ctx.range(u3, 2, 7);
    psx.setBits(&fx.unk6C[8], "actorId", actor_id);
    const tbl = v.g.D_800F45E0();
    for (0..16) |i| tbl[i] = fx;

    // Flat heights half the time: the scan's low and high then match.
    g_flat = ctx.chance(2);
    g_flat_val = ctx.range(i32, -3, 3);

    // The map dimensions word: height in the top bits (-2..20, sometimes 20),
    // width in bits 4-15 (0-20), noise in the bits neither field reads.
    const hgt: i32 = if (ctx.chance(4)) 20 else ctx.range(i32, -2, 20);
    const w: u32 = ctx.range(u32, 0, 20);
    const hu: u32 = @bitCast(hgt);
    g_dims = @bitCast((hu << 20) | (w << 4) | (ctx.range(u32, 0, 15) << 16) | ctx.range(u32, 0, 15));

    // The position: tile x and z around the map; sometimes raw 16-bit words.
    const bx = ctx.range(i32, -3, 22);
    const bz = ctx.range(i32, -3, 22);
    const pos: [*c]c_ushort = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    pos[0] = @bitCast(@as(i16, @intCast(bx * 128 + ctx.range(i32, 0, 127))));
    pos[2] = @bitCast(@as(i16, @intCast(bz * 128 + ctx.range(i32, 0, 127))));
    if (ctx.chance(6)) pos[0] = @bitCast(ctx.int(u16));
    if (ctx.chance(6)) pos[2] = @bitCast(ctx.int(u16));

    // The room kind for this run, and nine rooms of that kind.
    g_mode = ctx.range(u8, 0, 2);
    for (0..9) |i| {
        const r = ctx.alloc(v._mpdRoomSection3);
        rooms[i] = r;
        var t5: u3 = ctx.value(u3);
        var t10: u3 = ctx.value(u3);
        var t18: u2 = ctx.value(u2);
        switch (g_mode) {
            0 => {
                // solid: bit 1 of unk0_5 set
                t5 |= 2;
            },
            1 => {
                // skipped: unk0_10 >= 5, bit 1 of unk0_18 clear, bit 1 of unk0_5 clear
                t5 &= 5;
                t10 = ctx.range(u3, 5, 7);
                t18 &= 1;
            },
            else => {
                // scanned: bit 1 of unk0_5 clear; unk0_10 < 5 or bit 1 of unk0_18 set
                t5 &= 5;
                if (t10 >= 5) t18 |= 2;
            },
        }
        psx.setBits(r, "unk0_5", t5);
        psx.setBits(r, "unk0_10", t10);
        psx.setBits(r, "unk0_18", t18);
    }

    // Row: mostly 0-8, sometimes an edge at or past 9.
    const row: u32 = if (ctx.chance(5))
        ctx.pick(u32, &.{ 9, 10, 0xFFFF_FFFF, 0x8000_0000 })
    else
        ctx.range(u32, 0, 8);

    return .{ pos, @intCast(row) };
}

pub fn classify(args: Args) usize {
    const row: u32 = @intCast(args[1]);
    if (row >= 9) return 0;

    const width: u32 = (@as(u32, @bitCast(g_dims)) & 0xFFFF) >> 4;
    const height: u32 = @bitCast(g_dims >> 20);
    const pos = args[0];
    const bx: i32 = @as(i32, @as(i16, @bitCast(pos[0])) >> 7);
    const bz: i32 = @as(i32, @as(i16, @bitCast(pos[2])) >> 7);

    var any = false;
    for (0..9) |col| {
        const x: i32 = bx + @as(i32, @intCast(col)) - 4;
        const z: i32 = bz + @as(i32, @intCast(row)) - 4;
        if (@as(u32, @bitCast(x)) < width and @as(u32, @bitCast(z)) < height) any = true;
    }
    if (!any) return 1;
    return 2 + @as(usize, g_mode);
}
