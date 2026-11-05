//! zig_diff spec: func_8008CEB0 -> room_shadows.func_8008CEB0 (146C.c, room_shadows.inc).
//! Inputs: no arguments. A mode per iteration picks why the room's platform shadows stop or
//! go on: no section 12; a platform flagged unk0_9; no sprite (func_8009195C null); a floor cell
//! with unk0_17 set outside 15..18; the platform's altitude at or above the floor (the edge
//! `altitude == height` included); a depth outside 5..2047 (4, 2048 and the sign); or a drawn
//! shadow, whose size (shade 0..255, the clamp at 224) and position (any i32 / 4096, packed to
//! i16) are pushed through the stubs: func_8008DC7C's height (15 bits, sign-extended: the
//! 0x4000 edge), RotAverage4 (its depth, and the screen points and interpolation it writes are
//! mixes of the four vertices, so every vertex is compared), the floor cell, and the always
//! linked native hook, which returns a packet cursor.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008CEB0));

pub const classes = [_][:0]const u8{ "no-section", "flagged", "no-sprite", "cell-blocks", "above-floor", "depth-out", "drawn" };

var mode: u8 = 0;
var depth_result: c_int = 0;
var height_result: c_short = 0;
var cell: ?*v._mpdRoomSection3 = null;
var sprite: ?*c_int = null;

pub const stubs = struct {
    pub fn func_8009195C(ctx: *zd.Ctx, index: c_int) [*c]c_int {
        _ = ctx;
        _ = index;
        return if (sprite) |s| s else null;
    }
    pub fn func_8008B764(ctx: *zd.Ctx, x: c_uint, y: c_uint, flag: c_int) ?*v._mpdRoomSection3 {
        _ = ctx;
        _ = x;
        _ = y;
        _ = flag;
        return cell;
    }
    pub fn func_8008DC7C(ctx: *zd.Ctx, x: c_int, y: c_int) c_short {
        _ = ctx;
        _ = x;
        _ = y;
        return height_result;
    }
    pub fn RotAverage4(ctx: *zd.Ctx, v0: [*c]v.SVECTOR, v1: [*c]v.SVECTOR, v2: [*c]v.SVECTOR, v3: [*c]v.SVECTOR, sxy0: [*c]c_int, sxy1: [*c]c_int, sxy2: [*c]c_int, sxy3: [*c]c_int, p: [*c]c_int, flag: [*c]c_int) c_int {
        // the four vertices are a stack local of the function: copy them where the harness compares them
        const seen = ctx.alloc([4]v.SVECTOR);
        // x, y and z only: the pad word is uninitialised in the C (the port zeroes it)
        for ([_][*c]v.SVECTOR{ v0, v1, v2, v3 }, 0..) |vertex, k| {
            seen[k].vx = vertex[0].vx;
            seen[k].vy = vertex[0].vy;
            seen[k].vz = vertex[0].vz;
        }
        sxy0[0] = mix(v0[0].vx, v3[0].vz);
        sxy1[0] = mix(v1[0].vx, v1[0].vz);
        sxy2[0] = mix(v2[0].vx, v2[0].vz);
        sxy3[0] = mix(v3[0].vx, v0[0].vz);
        p[0] = mix(v0[0].vy, v1[0].vy +% v2[0].vy +% v3[0].vy);
        flag[0] = 0x1234;
        return depth_result;
    }
    pub fn pc_platform_shadow_native(ctx: *zd.Ctx, index: c_int, centre: [*c]const c_int, shade: c_int, ot_entry: [*c]c_uint, packet: [*c]c_uint) [*c]c_uint {
        _ = index;
        _ = shade;
        // the centre is a stack local of the function
        const seen = ctx.alloc([3]c_int);
        seen.* = .{ centre[0], centre[1], centre[2] };
        _ = ot_entry;
        return packet + 6;
    }
};

fn mix(a: i16, b: i16) c_int {
    return @bitCast((@as(u32, @as(u16, @bitCast(a)))) | (@as(u32, @as(u16, @bitCast(b))) << 16));
}

fn signExtend15(x: c_short) c_int {
    return (@as(c_int, x) << 17) >> 17;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    mode = ctx.range(u8, 0, 6);
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    scratch.unk0.raw = psx.addr(ctx.allocBytes(24 * 8 + 16, 16));
    scratch.unk4.raw = psx.addr(ctx.allocBytes(64, 16));
    const room = v.g.vs_battle_roomData();
    const n: usize = if (ctx.chance(4)) 3 else 1;
    const size = @sizeOf(v._mpdRoomSection12);
    if (mode == 0) {
        psx.put(&room.section12, null);
    } else {
        const objects: [*]v._mpdRoomSection12 = @ptrCast(@alignCast(ctx.allocBytes(n * size, 16)));
        psx.put(&room.section12, &objects[0]);
        var k: usize = 0;
        while (k < n) : (k += 1) fill(ctx, &objects[k]);
    }
    room.header.section12Len = @intCast(n * size + ctx.range(usize, 0, size - 1));
    sprite = if (mode == 2) null else @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    const cell_block = ctx.alloc(v._mpdRoomSection3);
    psx.setBits(cell_block, "unk0_17", if (mode == 3) 1 else ctx.int(u1));
    const blocked = mode == 3;
    psx.setBits(cell_block, "unk0_0", if (blocked) ctx.edge(u5, &.{ 0, 14, 19, 20, 31 }) else if (psx.getBits(cell_block, "unk0_17") == 1) ctx.edge(u5, &.{ 15, 16, 17, 18 }) else ctx.int(u5));
    cell = cell_block;
    height_result = ctx.edge(c_short, &.{ 0, 1, -1, 0x3FFF, 0x4000, 0x4001, -0x4000, -0x4001, 0x7FFF, -0x8000, 1000, -1000 });
    depth_result = if (mode == 5) ctx.edge(c_int, &.{ 4, 3, 2048, 0, -1, 99999, 5000 }) else ctx.edge(c_int, &.{ 5, 6, 100, 2046, 2047 });
    return .{};
}

fn fill(ctx: *zd.Ctx, object: *v._mpdRoomSection12) void {
    psx.setBits(object, "unk0_9", @intFromBool(mode == 1));
    psx.setBits(object, "unk0_0", ctx.int(u8));
    object.unkA8 = ctx.pick(i32, &.{ 0, 0x1000, -0x1000, 0x7FFFFFFF, -0x80000000, 0x80000, -0x80000, 0x7FFFF });
    object.unkB0 = ctx.pick(i32, &.{ 0, 0x1000, -0x1000, 0x7FFFFFFF, -0x80000000, 0x80000, -0x80000, 0x7FFFF });
    const height = signExtend15(height_result);
    const d: c_int = if (mode == 4) ctx.edge(c_int, &.{ 0, -1, -100 }) else ctx.edge(c_int, &.{ 1, 2, 446, 447, 448, 449, 1000, 30000 });
    const altitude = height -% d;
    const rest = ctx.range(i32, 0, 4095);
    object.unkAC = (-%altitude *% 4096) -% (if (altitude >= 0) rest else -rest);
}

pub fn classify(args: Args) usize {
    _ = args;
    return switch (mode) {
        0 => 0,
        1 => 1,
        2 => 2,
        3 => 3,
        4 => 4,
        5 => 5,
        else => 6,
    };
}
