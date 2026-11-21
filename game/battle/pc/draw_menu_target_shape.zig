//! A battle menu's target shape (D2: 58350.s; oracle pc/src/battle_gte.c).
const c = @import("c_battle_gte");
const v = @import("views_battle_gte");
const psx = @import("common").psx;
const pad = @import("common").pad;
const gte = @import("common").gte;
const sprite_item = @import("common").sprite_item;

fn shortAt(base: [*]u8, offset: isize) c_int {
    return psx.byteAt(i16, base, offset).*;
}

/// orig: vs_battle_drawSphereVolumeWireframe (pc/src/battle_gte.c)
/// Draws the shape `shape` of the battle menu state block (vs_battle_targetSphere): its
/// 0x32-style point list at +0x48 is turned by the shape's scale and rotation
/// (composed at scratchpad 0x398) and projected by the GTE; each point with a
/// nonzero pad draws a segment from the previous point (func_800C0990) with
/// an intensity from the pair's depth. Scratchpad 0x398.. holds the current
/// and previous projected point (SXY, depth / 4).
pub fn drawMenuTargetShape(shape: [*]u8, color_in: c_uint) callconv(.c) void {
    const block = v.g.vs_battle_targetSphere().*.?;
    var point: usize = @intFromPtr(&block.points);
    const scratch: [*]u32 = pad.many(u32, 0x398);
    const halves: [*]u16 = pad.many(u16, 0x398);
    const matrix: [*c]c.MATRIX = pad.at(c.MATRIX, 0x398);
    const scale = c.func_800C085C(shape + 4, shortAt(shape, 2));
    _ = c.RotMatrixYXZ_gte(@ptrFromInt(@intFromPtr(&block.unk24)), matrix);
    const factor = c.rcos(block.unk2A << 6);
    var scaling = c.VECTOR{ .vx = factor, .vy = factor, .vz = factor, .pad = 0 };
    _ = c.ScaleMatrix(matrix, &scaling);
    _ = c.vs_core_mathMultiplyMatrixIntoRight(scale, matrix);
    c.vs_render_loadFrameViewMatrix();
    const frame = gte.frame();
    // The light matrix (the composed rotation's five words) and the background colour, as the registers took them.
    const light: [5]c_uint = .{ scratch[0], scratch[1], scratch[2], scratch[3], scratch[4] };
    const background: [3]c_uint = .{ @bitCast(shortAt(shape, 8)), @bitCast(shortAt(shape, 0xa)), @bitCast(shortAt(shape, 0xc)) };
    const color = sprite_item.rgbCode(color_in, 0x44);
    var count: c_uint = @as(c_uint, @as(u16, @bitCast(block.unk32)));
    const divisor: c_int = if (shape[6] < shape[4]) shape[4] else shape[6];
    const bias: c_int = @as(c_int, block.unk20) << 1;
    var here: [2]f32 = .{ 0, 0 };
    var before: [2]f32 = .{ 0, 0 };
    while (count != 0) {
        count -%= 1;
        const ir = gte.lightTransform(&light, &background, @as(*align(1) const [3]i16, @ptrFromInt(point))); // gte_mvmva(1, 1, 0, 1, 0)
        const flag: c_int = psx.byteAt(i16, @as([*]u8, @ptrFromInt(point)), 6).*;
        point +%= 8;
        const projected = gte.project(&frame, &ir); // gte_rtps
        before = here;
        here = gte.lastProjection();
        scratch[0] = @bitCast(projected.sxy);
        scratch[1] = @bitCast(@as(c_int, @bitCast(projected.sz)) >> 2);
        if (flag != 0) {
            const sum: c_int = @as(c_int, @as(i16, @bitCast(halves[2]))) +% @as(c_int, @as(i16, @bitCast(halves[6]))) -% bias;
            // `div $zero` has no zero check: the R3000 then leaves LO = -1 for a non-negative dividend and 1 for a negative one.
            const depth: c_int = if (divisor != 0) @divTrunc(sum, divisor) else if (sum < 0) 1 else -1;
            sprite_item.wireNext(here[0], here[1], before[0], before[1]); // (the segment runs from the current point to the previous one)
            c.func_800C0990(@ptrCast(pad.many(u8, 0x398)), @ptrCast(pad.many(u8, 0x3A0)), color -% (@as(c_uint, @bitCast(flag)) << 25), 0x20 -% depth);
            sprite_item.wireClear(); // (a line the depth test sent away made no item: nothing is left for the next one)
        }
        var j: usize = 0;
        while (j < 3) : (j += 1) halves[4 + j] = halves[j];
    }
}
