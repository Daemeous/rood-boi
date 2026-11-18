//! Floating number digits: bouncing textured quads (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");
const sprite_item = @import("common").sprite_item;

/// A corner (x low, y high) and a texel (u low, v high) as the packets packed them.
fn xy(x: c_int, y: c_int) c_int {
    return (x & 0xFFFF) | (y << 16);
}
fn uv(u: c_int, w: c_int) c_int {
    return (u & 0xFF) | ((w & 0xFF) << 8);
}

/// orig: func_80094B0C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Draws the digits of a floating number at x, y as bouncing textured quads.
pub fn func_80094B0C(x_in: c_int, y_in: c_int, p: ?*v.D_800F1BAC_t) callconv(.c) void {
    const q = p.?;
    const colors = [_]v.CVECTOR{
        .{ .r = 128, .@"g" = 128, .b = 128, .cd = 0 },
        .{ .r = 0, .@"g" = 128, .b = 0, .cd = 0 },
        .{ .r = 128, .@"g" = 0, .b = 128, .cd = 0 },
        .{ .r = 0, .@"g" = 128, .b = 128, .cd = 0 },
    };
    const bounce = [_]i8{ 0, -6, -12, -14, -16, -10, -5, -1, 0, -3, -6, -7, -8, -5, -3, -1, 0, -2, -1, 0 };
    var x = x_in;
    var y = y_in;
    var frame: c_int = 15;
    var color: c_int = 0;
    var offset: c_int = 0;
    var i: c_int = 0;

    if (q.unkE >= 3) {
        if ((q.unkC & 0x8000) == 0) {
            y = y -% (@as(c_int, q.unkB) >> 1);
        } else {
            y = y +% (@as(c_int, q.unkB) >> 1);
        }
        i = c.vs_battle_drawValueLabelCaption(x, y, psx.cptr(c.D_800F1BAC_t, q));
        color = 0;
        offset = 0;
    } else {
        if (q.unkB < 16) {
            frame = @as(c_int, q.unkB);
        }
        color = (@as(c_int, q.unkE) & 1) << 1;
        i = @as(c_int, q.displayTextLen) *% 3;
        if ((q.unkC & 0x8000) == 0) {
            offset = bounce[@intCast(frame)];
            color +%= 1;
        } else {
            offset = bounce[@intCast(frame +% 4)];
        }
    }

    x = x +% (i -% 6);

    const ci: usize = @intCast(color);
    const scratch = common.scratch.get();
    // The slot 20 bytes before the table the scratchpad names.
    const ot: ?*anyopaque = @ptrFromInt(@intFromPtr(psx.get(scratch.unk4).?) -% 20);

    i = @as(c_int, q.displayTextLen) -% 1;
    while (i >= 0) : ({
        i -%= 1;
        x -%= 6;
    }) {
        const digitY: c_int = y +% offset;
        offset = @divTrunc(offset, 2);
        if (psx.conv(c_uint, x +% 8) < 328) {
            // The glyph of the character at displayText[i], in this number's colour.
            const glyph: c_int = @as(c_int, psx.byteAt(i8, q, @as(isize, i) + 20).*) -% '0';
            const base: c_int = glyph *% 8;
            const rgb: c_int = @as(c_int, colors[ci].r) | (@as(c_int, colors[ci].@"g") << 8) | (@as(c_int, colors[ci].b) << 16);
            _ = sprite_item.ft4(ot, false, false, (768 & 0x3ff) >> 6, (223 << 6) | ((832 >> 4) & 0x3f), rgb,
                &[4]c_int{ xy(x, digitY -% 6), xy(x +% 8, digitY -% 6), xy(x, digitY +% 6), xy(x +% 8, digitY +% 6) },
                &[4]c_int{ uv(base +% 64, 0), uv(base +% 72, 0), uv(base +% 64, 12), uv(base +% 72, 12) });
        }
    }
}
