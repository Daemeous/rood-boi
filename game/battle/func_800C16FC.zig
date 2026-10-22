//! Draws the fading trail of 16 line segments between two packed points, up to `count` steps (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C16FC (src/BATTLE/BATTLE.PRG/58578.c)
/// Adds the fading line segments of one trail from packed point x0 to packed point x1, with `count` steps shown.
pub fn func_800C16FC(x0_in: c_int, x1_in: c_int, count: c_int) callconv(.c) void {
    const scratch = v.g.vs_scratch();
    var signX: c_int = 1;
    var signY: c_int = 1;
    const y1: c_int = x1_in >> 16;
    const x1: c_int = @as(c_int, psx.conv(i16, x1_in));
    var y0: c_int = x0_in >> 16;
    const x0: c_int = @as(c_int, psx.conv(i16, x0_in));
    var length: c_int = y0 -% y1;
    if (length < 0) {
        signY = -1;
        length = -%length;
    }
    if ((length & 1) != 0) {
        y0 -%= signY;
    }
    length >>= 1;
    const midX: c_int = x0 +% length;
    var dx: c_int = x1 -% midX;
    if (dx < 0) {
        signX = -1;
        dx = -%dx;
    }
    length = length *% 2 +% dx;
    var segmentEnd: c_int = length;
    var segmentStart: c_int = 0;
    const y1Bits: c_int = y1 << 16;

    var i: c_int = 0;
    while (i < 16) : ({
        i += 1;
        segmentEnd +%= length;
        segmentStart +%= length;
    }) {
        const level: c_int = i -% count +% 16;
        if (count < i or level < 0) continue;
        var gray: c_int = ((level *% 3) << 6) >> 4;
        gray |= gray << 8;
        const color: c_int = gray | ((((level *% 255) +% ((16 -% level) *% 32)) >> 4) << 16);
        const start: c_int = segmentStart >> 4;
        if (start < dx) {
            const end: c_int = segmentEnd >> 4;
            if (end < dx) {
                c.vs_battle_addTile(
                    @ptrCast(@alignCast(psx.get(scratch.unk8))),
                    color | 0x42000000,
                    ((x1 -% start *% signX) & 0xFFFF) | y1Bits,
                    ((x1 -% (end -% 1) *% signX) & 0xFFFF) | y1Bits,
                );
            } else {
                const lineColor: c_int = color | 0x42000000;
                c.vs_battle_addTile(
                    @ptrCast(@alignCast(psx.get(scratch.unk8))),
                    lineColor,
                    ((x1 -% start *% signX) & 0xFFFF) | y1Bits,
                    ((midX +% signX) & 0xFFFF) | y1Bits,
                );
                const tail: c_int = length *% (15 -% i);
                if (y1 != y0 -% ((tail >> 4) *% signY)) {
                    c.vs_battle_addTile(
                        @ptrCast(@alignCast(psx.get(scratch.unk8))),
                        lineColor,
                        (midX & 0xFFFF) | y1Bits,
                        ((x0 +% (tail >> 5)) & 0xFFFF) |
                            ((y0 -% (((tail +% 16) >> 4) *% signY)) << 16),
                    );
                }
            }
        } else {
            const far2: c_int = length *% (16 -% i);
            const near2: c_int = length *% (15 -% i);
            c.vs_battle_addTile(
                @ptrCast(@alignCast(psx.get(scratch.unk8))),
                color | 0x42000000,
                ((x0 +% (far2 >> 5)) & 0xFFFF) | ((y0 -% ((far2 >> 4) *% signY)) << 16),
                ((x0 +% (near2 >> 5)) & 0xFFFF) |
                    ((y0 -% (((near2 +% 16) >> 4) *% signY)) << 16),
            );
        }
    }
    c.vs_battle_insertTpage(
        @as(c_int, @bitCast(@as(c_uint, 0xE1000020))),
        @ptrCast(@alignCast(psx.get(scratch.unk8))),
    );
}
