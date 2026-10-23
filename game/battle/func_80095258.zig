//! Draws a combat label strip as textured quads, sliding and fading with its
//! animation phase (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80095258 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Draws the combat label strip for `p` at (x, y), sliding in or out and fading by its phase.
pub fn func_80095258(x_arg: c_int, y: c_int, p: ?*v.D_800F1BAC_t) callconv(.c) void {
    const slide = [4]i16{ 32, 14, 6, 2 };
    const fade = [4]i16{ 5, 9, 13, 15 };

    const q = p.?;
    const labelDef = psx.at(v.g.D_800690B4(), q.unkC);
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
        const r = psx.at(v.g.D_80068FAC(), labelDef.labels[idx]);
        rects[idx] = r;
        width +%= @as(c_int, r.w);
    }

    x -%= @divTrunc(width, 2);
    if (flagE) {
        width = 0;
    } else {
        width = @as(c_int, labelDef.color);
    }

    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    var cursor: u32 = psx.rawOf(scratch.unk0);

    i = 0;
    while (i < @as(c_int, labelDef.count)) : (i += 1) {
        const idx: usize = @intCast(i);
        const rect = rects[idx];
        const w: c_int = rect.w;
        if (x < 320 and x +% w >= 0) {
            const prim: *align(1) v.POLY_FT4 = @ptrFromInt(@as(usize, cursor));

            psx.setBits(@as(*align(1) v.P_TAG, @ptrCast(prim)), "len", 9);
            prim.code = 0x2c;
            prim.x0 = psx.conv(i16, x);
            prim.y0 = psx.conv(i16, y -% 6);
            prim.x1 = psx.conv(i16, x +% w);
            prim.y1 = psx.conv(i16, y -% 6);
            prim.x2 = psx.conv(i16, x);
            prim.y2 = psx.conv(i16, y +% 6);
            prim.x3 = psx.conv(i16, x +% w);
            prim.y3 = psx.conv(i16, y +% 6);
            prim.code &= 0xFE;

            const col = psx.at(v.g.D_80069134(), width);
            if (step == 5) {
                prim.r0 = psx.conv(u8, @divTrunc(@as(c_int, col.r), 2) +% 96);
                prim.g0 = psx.conv(u8, @divTrunc(@as(c_int, col.g), 2) +% 96);
                prim.b0 = psx.conv(u8, @divTrunc(@as(c_int, col.b), 2) +% 96);
            } else if (step == 4) {
                prim.r0 = psx.conv(u8, @divTrunc(@as(c_int, col.r), 2));
                prim.g0 = psx.conv(u8, @divTrunc(@as(c_int, col.g), 2));
                prim.b0 = psx.conv(u8, @divTrunc(@as(c_int, col.b), 2));
            } else {
                const f: c_int = psx.at(&fade, step).*;
                prim.r0 = psx.conv(u8, @divTrunc(@as(c_int, col.r) *% f, 32));
                prim.g0 = psx.conv(u8, @divTrunc(@as(c_int, col.g) *% f, 32));
                prim.b0 = psx.conv(u8, @divTrunc(@as(c_int, col.b) *% f, 32));
            }

            prim.u0 = psx.conv(u8, @as(c_int, rect.x));
            prim.v0 = psx.conv(u8, @as(c_int, rect.y));
            prim.u1 = psx.conv(u8, @as(c_int, rect.x) +% w);
            prim.v1 = psx.conv(u8, @as(c_int, rect.y));
            prim.u2 = psx.conv(u8, @as(c_int, rect.x));
            prim.v2 = psx.conv(u8, @as(c_int, rect.y) +% 12);
            prim.u3 = psx.conv(u8, @as(c_int, rect.x) +% w);
            prim.v3 = psx.conv(u8, @as(c_int, rect.y) +% 12);

            prim.code |= 0x02;
            if (step != 4) {
                // setTPage(prim, 0, 1, 448, 256); setClut(prim, 960, 220)
                prim.tpage = 0x37;
                prim.clut = 14140;
            } else {
                // setTPage(prim, 0, 0, 448, 256); setClut(prim, 976, 220)
                prim.tpage = 0x17;
                prim.clut = 14141;
            }

            const ot: usize = @as(usize, psx.rawOf(scratch.unk4)) -% 20;
            c.AddPrim(@as(?*anyopaque, @ptrFromInt(ot)), prim);
            cursor +%= @sizeOf(v.POLY_FT4);
        }
        x +%= w;
    }

    psx.put(&scratch.unk0, @as(?*anyopaque, @ptrFromInt(@as(usize, cursor))));
}
