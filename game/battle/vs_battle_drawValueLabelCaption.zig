//! Builds a text box's frame quads (a body and a tail) into the ordering table and returns the box width (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const sprite_item = @import("common").sprite_item;

/// A corner (x low, y high) and a texel (u low, v high) as the packets packed them (u8 and i16 truncated).
fn xy(x: c_int, y: c_int) c_int {
    return (x & 0xFFFF) | (y << 16);
}
fn uv(u: c_int, w: c_int) c_int {
    return (u & 0xFF) | ((w & 0xFF) << 8);
}

/// orig: vs_battle_drawValueLabelCaption (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Lays out the two frame primitives of a text box ending at x, y and returns its width.
pub fn vs_battle_drawValueLabelCaption(x0: c_int, y: c_int, p: ?*v.D_800F1BAC_t) callconv(.c) c_int {
    const q = p.?;
    const scratch = common.scratch.get();
    const idx: c_int = @as(c_int, q.unkE) -% 3;
    const rect = psx.at(v.g.vs_battle_labelCaptionRects(), psx.at(v.g.D_800E861C(), idx).*);
    var sign: c_int = q.unkC;
    const width: c_int = @divTrunc(@as(c_int, q.displayTextLen) *% 6 +% @as(c_int, rect.w) +% 12, 2);
    var x = x0 -% width;
    // The slot 20 bytes before the table the scratchpad names.
    const ot: ?*anyopaque = @ptrFromInt(@intFromPtr(psx.get(scratch.unk4).?) -% 20);

    // The label (raw texels) and, past it, a sign.
    const rx: c_int = rect.x;
    const ry: c_int = rect.y;
    const rw: c_int = rect.w;
    _ = sprite_item.ft4(ot, true, false, ((256 & 0x100) >> 4) | ((448 & 0x3ff) >> 6), (220 << 6) | ((976 >> 4) & 0x3f), 0,
        &[4]c_int{ xy(x, y -% 6), xy(x +% rw, y -% 6), xy(x, y +% 6), xy(x +% rw, y +% 6) },
        &[4]c_int{ uv(rx, ry), uv(rx +% rw, ry), uv(rx, ry +% 12), uv(rx +% rw, ry +% 12) });
    sign = if ((sign & 0x8000) != 0) 0 else 8;

    x = x +% (rw +% 4);
    _ = sprite_item.ft4(ot, true, false, (768 & 0x3ff) >> 6, (223 << 6) | ((864 >> 4) & 0x3f), 0,
        &[4]c_int{ xy(x, y -% 5), xy(x +% 8, y -% 5), xy(x, y +% 5), xy(x +% 8, y +% 5) },
        &[4]c_int{ uv(sign +% 154, 76), uv(sign +% 162, 76), uv(sign +% 154, 86), uv(sign +% 162, 86) });

    return width;
}
