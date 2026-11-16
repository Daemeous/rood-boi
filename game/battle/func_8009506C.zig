//! Appends the four textured quads of a bar, sized by a step from `data.unkB`, to the
//! ordering table (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const sprite_item = @import("common").sprite_item;

/// A corner (x low, y high) and a texel (u low, v high) as the packets packed them.
fn xy(x: c_int, y: c_int) c_int {
    return (x & 0xFFFF) | (y << 16);
}
fn uv(u: c_int, w: c_int) c_int {
    return (u & 0xFF) | ((w & 0xFF) << 8);
}

/// orig: func_8009506C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Builds the four quads of a bar at (left, centerY) into the packet cursor and links them into the ordering table.
pub fn func_8009506C(left: c_int, centerY: c_int, arg2: ?*v.D_800F1BAC_t) callconv(.c) void {
    const data = arg2.?;
    const unkB: c_int = data.unkB;
    const step: c_int = if (unkB < 4) unkB else if (unkB >= 27) 30 -% unkB else 4;

    const scratch = common.scratch.get();
    const heights = v.g.vs_battle_lockedLabelHalfHeights();
    const rects = v.g.vs_battle_lockedLabelPieces();

    var x: c_int = left -% 16;
    const half: c_int = @as(c_int, psx.at(heights, step).*);
    const y0: c_int = centerY -% half;
    const y1: c_int = centerY +% half;

    // The slot 20 bytes before the table the scratchpad names.
    const ot: ?*anyopaque = @ptrFromInt(@intFromPtr(psx.get(scratch.unk4).?) -% 20);

    var i: usize = 0;
    while (i < 4) : (i += 1) {
        const rect = rects[i];
        const w: c_int = rect.w;
        if ((x < 320) and ((x +% w) >= 0)) {
            const rx: c_int = rect.x;
            const ry: c_int = rect.y;
            // A raw-textured, semi-transparent quad (page 768, CLUT 864, 223).
            _ = sprite_item.ft4(ot, true, true, (768 & 0x3ff) >> 6, (223 << 6) | ((864 >> 4) & 0x3f), 0,
                &[4]c_int{ xy(x, y0), xy(x +% w, y0), xy(x, y1), xy(x +% w, y1) },
                &[4]c_int{ uv(rx, ry), uv(rx +% w, ry), uv(rx, ry +% 10), uv(rx +% w, ry +% 10) });
        }

        x +%= w;
    }
}
