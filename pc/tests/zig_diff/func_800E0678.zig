//! zig_diff spec: func_800E0678 -> func_800E0678.func_800E0678 (771A8.c).
//! Inputs: a random directionalCacheState (tile and previous points, the
//! minimum height) and a direction 0-30. The scratchpad holds the
//! directionalCacheBits, a direction table with random dx/dz (the 2-bit
//! fields are -2..1, so x and z can leave the map) and a pointer to an arena
//! tile table. The map bounds are an arena object, set to the point's own
//! x/z about a third of the time so every ordered comparison sees equal sides.
//! func_800E02B4 and func_800E42EC are stubs: they write state fields the C
//! reads back, and func_800E42EC sometimes returns exactly unk14 - minimumHeight
//! so its `<` sees equal sides.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E0678));

pub const classes = [_][:0]const u8{ "tested", "out-of-bounds", "blocked", "previous", "clear" };

/// The state and map bounds the stubs read back (set by prepare).
var cur: ?*v.directionalCacheState = null;
var bnd: ?*v.D_800F58BC_t = null;

pub const stubs = struct {
    pub fn func_800E02B4(ctx: *zd.Ctx, state: anytype, direction: anytype, ahead: anytype) c_int {
        _ = state;
        _ = direction;
        _ = ahead;
        const s = cur orelse return 0;
        // The obstruction check may move the state's heights and previous cell.
        if (ctx.value(u8) < 64) s.minimumHeight = ctx.value(i16);
        if (ctx.value(u8) < 64) s.previous.p.x = ctx.value(u8);
        if (ctx.value(u8) < 128) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800E42EC(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        const st = cur orelse return 0;
        const b = bnd orelse return 0;
        // Sometimes exactly the value that makes the C's `<` see equal sides.
        if (ctx.value(u8) < 64) return b.unk14 - @as(c_int, st.minimumHeight);
        return @as(c_int, ctx.value(i16));
    }
};

fn clampU8(x: c_int) u8 {
    return @intCast(std.math.clamp(x, 0, 255));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const st = ctx.alloc(v.directionalCacheState);
    cur = st;
    const dir: c_int = ctx.range(c_int, 0, 30);
    const d: usize = @intCast(dir);

    // Direction table in the scratchpad: one byte per direction, dx in bits 0-1,
    // dz in bits 4-5 (signed 2-bit fields), other bits random.
    const dtab: [*]u8 = @ptrFromInt(0x1F8003EC);
    const dxv = ctx.range(i8, -2, 1);
    const dzv = ctx.range(i8, -2, 1);
    dtab[d] = (@as(u8, @bitCast(dxv)) & 3) | ((@as(u8, @bitCast(dzv)) & 3) << 4) | (ctx.int(u8) & 0xCC);

    st.tile.p.x = ctx.pick(u8, &.{ 0, 1, 2, 254, 255 });
    st.tile.p.z = ctx.pick(u8, &.{ 0, 1, 2, 254, 255 });
    const x: c_int = @as(c_int, st.tile.p.x) + @as(c_int, dxv);
    const z: c_int = @as(c_int, st.tile.p.z) + @as(c_int, dzv);

    st.previous.p.x = if (ctx.chance(3)) clampU8(x) else ctx.int(u8);
    st.previous.p.z = if (ctx.chance(3)) clampU8(z) else ctx.int(u8);
    st.minimumHeight = ctx.int(i16);

    // Map bounds: the arena object the scratchpad-free global points at.
    const b = ctx.alloc(v.D_800F58BC_t);
    b.maxX = if (ctx.chance(3)) clampU8(x) else if (ctx.chance(2)) 255 else ctx.int(u8);
    b.minX = if (ctx.chance(3)) clampU8(x) else if (ctx.chance(2)) 0 else ctx.int(u8);
    b.maxZ = if (ctx.chance(3)) clampU8(z) else if (ctx.chance(2)) 255 else ctx.int(u8);
    b.minZ = if (ctx.chance(3)) clampU8(z) else if (ctx.chance(2)) 0 else ctx.int(u8);
    // Kept within +-2^30 so the stub's unk14 - minimumHeight never overflows.
    b.unk14 = if (ctx.chance(4)) 0 else ctx.range(i32, -(1 << 30), 1 << 30);
    v.g.D_800F58BC().* = b;
    bnd = b;

    // Tile table: unsigned short[][32] in the arena, its address in the
    // scratchpad word at 0x1F8003C0. A pad block in front catches the negative
    // rows a wrong bound would reach.
    _ = ctx.allocBytes(256, 16);
    const tiles = ctx.allocBytes(17408, 16);
    @as(*u32, @ptrFromInt(0x1F8003C0)).* = psx.addr(tiles);

    return .{ st, dir };
}

pub fn classify(args: Args) usize {
    const st = args[0].?;
    const d: u5 = @intCast(args[1]);
    const tested = @as(*const u8, @ptrFromInt(0x1F8003D0)).*;
    if (((@as(u32, tested) >> d) & 1) != 0) return 0;

    const e = @as([*]const u8, @ptrFromInt(0x1F8003EC))[d];
    const dx: c_int = @as(i2, @bitCast(@as(u2, @truncate(e))));
    const dz: c_int = @as(i2, @bitCast(@as(u2, @truncate(e >> 4))));
    const x: c_int = @as(c_int, st.tile.p.x) + dx;
    const z: c_int = @as(c_int, st.tile.p.z) + dz;

    const b = bnd.?;
    if (x > b.maxX or x < b.minX or z > b.maxZ or z < b.minZ) return 1;

    const tp = @as(*const u32, @ptrFromInt(0x1F8003C0)).*;
    const rows: [*]const u16 = @ptrFromInt(@as(usize, tp));
    const tile = rows[@as(usize, @intCast(z)) * 32 + @as(usize, @intCast(x))];
    if ((tile & 0x40) != 0) return 2;

    if (st.previous.p.x == x and st.previous.p.z == z) return 3;
    return 4;
}
