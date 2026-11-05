//! zig_diff spec: func_800C2E24 -> draw_area_map_screen.drawAreaMapScreen (D2: pc/src/map_view.c).
//! Inputs: the menu state block the global D_800EB9B8 points at (random
//! bytes, with the view turn at +0x22 chosen so each of the four rotations
//! occurs, the 9x9 cell words at +0x4370 made of empty cells (a floor above
//! the view height), floors of every height up to the 0x30 clamp and
//! untouched cells, and the marker grid at +0x4AB8 empty or dense), the
//! view origin (height and position), a view matrix and the circle's scale.
//! The reciprocal, neighbour-pair and sine tables are filled as the game's
//! (the reciprocal one's index 0 is any value). The GTE is a stub: MFC2
//! returns points mostly on the 224-row screen and depths below 0x2000, so
//! edges cross rows, span rows overflow their counts, walls cut and the
//! circle clips; the packet buffer and ordering table (OT slots 0..0x7FF)
//! are in the arena. pc_hw_span_shape and pc_hw_anchor_packets are
//! recording stubs, as is the pc_unimplemented of a row buffer past the
//! scratchpad's end.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_map_view");
const c = @import("c_map_view");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").drawAreaMapScreen));

pub const classes = [_][:0]const u8{ "rotation-0", "rotation-1", "rotation-2", "rotation-3", "markers", "tall-cells" };

var scenario: usize = 0;

fn cell(ctx: *zd.Ctx, y: c_int, tall: bool) u32 {
    const kind = ctx.range(u8, 0, if (tall) 3 else 5);
    const flag: u32 = ctx.range(u32, 0, 1);
    switch (kind) {
        0, 1 => { // empty: a floor above the view height, no ceiling
            const low: i32 = y + ctx.range(i32, 1, 0x1000);
            return (@as(u32, @bitCast(@as(i32, @min(low, 0x3FFF)))) & 0x7FFF) << 2 | flag;
        },
        2, 3 => { // a floor at or below the view height (0 to beyond the 0x30 clamp), visible
            const low: i32 = y - ctx.edge(i32, &.{ 0, 1, 0x2F, 0x30, 0x31, ctx.range(i32, 0, 0x60) });
            return (@as(u32, @bitCast(@max(@min(low, 0x3FFF), -0x4000))) & 0x7FFF) << 2 | 1;
        },
        else => return ctx.int(u32),
    }
}

pub fn prepare(ctx: *zd.Ctx) Args {
    scenario = ctx.range(usize, 0, classes.len - 1);
    mode = if (ctx.chance(2)) 0 else ctx.range(u8, 1, 7);
    const y: c_int = ctx.edge(c_int, &.{ 0, 8, 100, -40 }); // the origin's y is a short
    const block = ctx.allocBytes(0x4B10, 16);
    for (block[0..0x4B10]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800EB9B8().* = @intCast(@intFromPtr(block));
    const rotation: u16 = if (scenario < 4) @intCast(scenario) else ctx.range(u16, 0, 3);
    std.mem.writeInt(i16, block[0x22..0x24], @bitCast(@as(u16, 0x200) +% rotation * 0x400 +% ctx.range(u16, 0, 0x3FF)), .little);
    const cells: [*]u32 = @ptrCast(@alignCast(block + 0x4370));
    var i: usize = 0;
    while (i < 81) : (i += 1) cells[i] = cell(ctx, y, scenario == 5);
    i = 0;
    while (i < 81) : (i += 1) {
        const dense = scenario == 4;
        block[0x4ab8 + i] = if (dense and !ctx.chance(4)) ctx.range(u8, 1, 255) else if (ctx.chance(10)) ctx.int(u8) else 0;
    }

    const recip = v.g.D_800EA130();
    recip[0] = ctx.int(u16);
    i = 1;
    while (i < 0x200) : (i += 1) recip[i] = @intCast(0x8000 / i);
    const pairs = v.g.D_800EA6B0();
    i = 0;
    while (i < 0xb4) : (i += 1) pairs[i] = @as(u16, ctx.range(u8, 0, 80)) | (@as(u16, ctx.range(u8, 0, 80)) << 8);
    const sine = v.g.D_800EA818();
    i = 0;
    while (i < 40) : (i += 1) {
        // round(sin(i * 2 pi / 32) * 4096), tabulated (math.sin/round would pull libm into the harness).
        const table = [32]i16{ 0, 799, 1567, 2276, 2896, 3406, 3784, 4017, 4096, 4017, 3784, 3406, 2896, 2276, 1567, 799, 0, -799, -1567, -2276, -2896, -3406, -3784, -4017, -4096, -4017, -3784, -3406, -2896, -2276, -1567, -799 };
        sine[i] = table[i % 32];
    }

    const packets = ctx.allocBytes(0x10000, 16);
    const ot = ctx.allocBytes(0x800 * 4 + 16, 16);
    for (ot[0 .. 0x800 * 4]) |*byte| byte.* = ctx.int(u8);
    const scratch: [*]volatile u32 = @ptrFromInt(zd.scratchpad);
    scratch[0] = @intCast(@intFromPtr(packets));
    scratch[1] = @intCast(@intFromPtr(ot));

    const origin = ctx.alloc(c.SVECTOR);
    origin.vx = ctx.int(i16);
    origin.vy = @intCast(y);
    origin.vz = ctx.int(i16);
    const m = ctx.alloc(c.MATRIX);
    for (&m.m) |*row| for (row) |*value| {
        value.* = ctx.pick(c_short, &.{ 0x1000, -0x1000, 0, 0x0800 });
    };
    return .{ origin, m, ctx.pick(c_int, &.{ 0, 0x100, 0x1000, 0x2000 }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return scenario;
}

/// How the projected rows of one iteration are drawn: any on the screen, all equal (the circle walk
/// meets its top point again), all above the screen, clustered on the screen's edges and on each other
/// (the boundary comparisons), or with any depth (past the ordering table's 0x800 entries), or all 0x7FF.
var mode: u8 = 0;

pub const stubs = struct {
    /// Points mostly on the 224-row screen, depths below 0x2000.
    pub fn MFC2(ctx: *zd.Ctx, reg: c_int) c_uint {
        const r = ctx.value(u32);
        if (reg == 19) return if (mode == 7) r else if (mode == 6) (0x1FFC | (r & 3)) else r & 0x1FFF; // mode 6: the depth 0x7FF the ordering table ends at
        const small: i32 = @intCast((r >> 16) % 5);
        const row: i32 = switch (mode) {
            1 => 100,
            2 => -10 - small,
            3 => 0xde + small,
            4 => -2 + small,
            5 => 100 + small,
            else => @as(i32, @intCast((r >> 16) % 300)) - 30,
        };
        return (@as(u32, @as(u16, @bitCast(@as(i16, @intCast(row))))) << 16) | (r & 0x1FF);
    }
};

/// The message pc_unimplemented gets is the C's literal on one side and the port's on the other: compared by text.
pub fn textArgument(call: []const u8, index: usize) bool {
    return std.mem.eql(u8, call, "pc_unimplemented") and index == 0;
}
