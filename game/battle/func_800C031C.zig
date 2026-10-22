//! Builds the 9x9 grid record's perimeter edge table (src/BATTLE/BATTLE.PRG/573B8.c).
const psx = @import("common").psx;
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: func_800C031C (src/BATTLE/BATTLE.PRG/573B8.c)
/// Allocates and clears the grid record if needed, then writes its 720 edge entries around the grid.
pub fn func_800C031C() callconv(.c) void {
    if (v.g.D_800EB9B8().* == null) {
        const raw: ?*align(4) anyopaque = @alignCast(c.vs_main_allocHeap(0x4B1C));
        v.g.D_800EB9B8().* = @ptrCast(raw);
    }
    c.vs_battle_rMemzero(v.g.D_800EB9B8().*, 0x4B1C);
    var index: c_int = 0;
    v.g.D_800EB9B8().*.?.unk2D = 0xFF;
    const edges: [*]align(1) i16 = @ptrCast(psx.byteAt(i16, v.g.D_800EB9B8().*.?, 0x44B4));

    var x: c_int = 0;
    var y: c_int = 0;

    // Top edge, alternating row and column segments.
    y = 0;
    while (true) : (y +%= 1) {
        x = 0;
        while (x < 9) : (x +%= 1) {
            edges[@intCast(index)] = psx.conv(i16, x +% (y << 4) +% ((x +% 1) << 8) +% (y << 12));
            index +%= 1;
        }
        if (y == 9) break;
        x = 0;
        while (x < 10) : (x +%= 1) {
            edges[@intCast(index)] = psx.conv(i16, x +% (y << 4) +% (x << 8) +% ((y +% 1) << 12));
            index +%= 1;
        }
    }

    // Right edge.
    x = 0;
    while (true) : (x +%= 1) {
        y = 9;
        while (y > 0) : (y -%= 1) {
            edges[@intCast(index)] = psx.conv(i16, x +% (y << 4) +% (x << 8) +% ((y -% 1) << 12));
            index +%= 1;
        }
        if (x == 9) break;
        y = 9;
        while (y >= 0) : (y -%= 1) {
            edges[@intCast(index)] = psx.conv(i16, x +% (y << 4) +% ((x +% 1) << 8) +% (y << 12));
            index +%= 1;
        }
    }

    // Bottom edge.
    y = 9;
    while (true) : (y -%= 1) {
        x = 9;
        while (x > 0) : (x -%= 1) {
            edges[@intCast(index)] = psx.conv(i16, x +% (y << 4) +% ((x -% 1) << 8) +% (y << 12));
            index +%= 1;
        }
        if (y == 0) break;
        x = 9;
        while (x >= 0) : (x -%= 1) {
            edges[@intCast(index)] = psx.conv(i16, x +% (y << 4) +% (x << 8) +% ((y -% 1) << 12));
            index +%= 1;
        }
    }

    // Left edge.
    x = 9;
    while (true) : (x -%= 1) {
        y = 0;
        while (y < 9) : (y +%= 1) {
            edges[@intCast(index)] = psx.conv(i16, x +% (y << 4) +% (x << 8) +% ((y +% 1) << 12));
            index +%= 1;
        }
        if (x == 0) break;
        y = 0;
        while (y < 10) : (y +%= 1) {
            edges[@intCast(index)] = psx.conv(i16, x +% (y << 4) +% ((x -% 1) << 8) +% (y << 12));
            index +%= 1;
        }
    }
}
