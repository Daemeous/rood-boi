//! Draws a combat label strip as textured quads, sliding and fading with its
//! animation phase (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
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

/// orig: func_80095258 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Draws the combat label strip for `p` at (x, y), sliding in or out and fading by its phase.
pub fn func_80095258(x_arg: c_int, y: c_int, p: ?*v.D_800F1BAC_t) callconv(.c) void {
    const slide = [4]i16{ 32, 14, 6, 2 };
    const fade = [4]i16{ 5, 9, 13, 15 };

    const q = p.?;
    const labelDef = psx.at(v.g.vs_battle_statusLabelLayouts(), q.unkC);
    if (labelDef.count == 0) return;

    var x = x_arg;
    var width: c_int = 0;
    var step: c_int = undefined;
    const unkB: c_int = q.unkB;
    const flagE = (q.unkE & 1) != 0;

    // unkB > 30 makes step negative: the original reads before the table.
    if (unkB < 4) {
        step = unkB;
        if (!flagE) x +%= @as(c_int, psx.at(&slide, step).*);
    } else if (unkB < 6) {
        step = 5;
    } else if (unkB >= 27) {
        step = 30 -% unkB;
        if (!flagE) x -%= @as(c_int, psx.at(&slide, step).*);
    } else {
        step = 4;
        if (unkB >= 25) step = 5;
    }

    if (flagE and unkB >= 15) {
        width = unkB -% 15;
        if (width > 4) width = 4;
        c.func_800957D0(x, y, width);
    }

    // labels has 2 entries: a count above 2 is out of bounds in the original.
    var rects: [2]*v.RECT = undefined;
    width = 0;
    var i: c_int = 0;
    while (i < @as(c_int, labelDef.count)) : (i += 1) {
        const idx: usize = @intCast(i);
        const r = psx.at(v.g.vs_battle_labelCaptionRects(), labelDef.labels[idx]);
        rects[idx] = r;
        width +%= @as(c_int, r.w);
    }

    x -%= @divTrunc(width, 2);
    if (flagE) {
        width = 0;
    } else {
        width = @as(c_int, labelDef.color);
    }

    const scratch = common.scratch.get();

    i = 0;
    while (i < @as(c_int, labelDef.count)) : (i += 1) {
        const idx: usize = @intCast(i);
        const rect = rects[idx];
        const w: c_int = rect.w;
        if (x < 320 and x +% w >= 0) {
            const col = psx.at(v.g.vs_battle_statusLabelColors(), width);
            var r: c_int = undefined;
            var g: c_int = undefined;
            var b: c_int = undefined;
            if (step == 5) {
                r = @divTrunc(@as(c_int, col.r), 2) +% 96;
                g = @divTrunc(@as(c_int, col.g), 2) +% 96;
                b = @divTrunc(@as(c_int, col.b), 2) +% 96;
            } else if (step == 4) {
                r = @divTrunc(@as(c_int, col.r), 2);
                g = @divTrunc(@as(c_int, col.g), 2);
                b = @divTrunc(@as(c_int, col.b), 2);
            } else {
                const f: c_int = psx.at(&fade, step).*;
                r = @divTrunc(@as(c_int, col.r) *% f, 32);
                g = @divTrunc(@as(c_int, col.g) *% f, 32);
                b = @divTrunc(@as(c_int, col.b) *% f, 32);
            }
            const rgb: c_int = (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16);
            const rx: c_int = rect.x;
            const ry: c_int = rect.y;
            // A semi-transparent quad (the page and CLUT of the label's colour: step 4 has its own).
            _ = sprite_item.ft4(@as(?*anyopaque, @ptrFromInt(@as(usize, psx.rawOf(scratch.unk4)) -% 20)), false, true,
                if (step != 4) 0x37 else 0x17, if (step != 4) 14140 else 14141, rgb,
                &[4]c_int{ xy(x, y -% 6), xy(x +% w, y -% 6), xy(x, y +% 6), xy(x +% w, y +% 6) },
                &[4]c_int{ uv(rx, ry), uv(rx +% w, ry), uv(rx, ry +% 12), uv(rx +% w, ry +% 12) });
        }
        x +%= w;
    }

}
