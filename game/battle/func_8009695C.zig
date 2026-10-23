//! Draws one row of a text-box entry as polygon primitives: two icons, a separator and the digits of its text (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009695C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Builds the textured quads of one text-box row into the ordering table and advances the primitive cursor.
pub fn func_8009695C(x_in: c_int, y_in: c_int, p: ?*v.D_800F1BAC_t) callconv(.c) void {
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    var x = x_in;
    var y = y_in;
    var prim: [*]v.POLY_FT4 = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
    const pp = p.?;
    y = y -% @as(c_int, pp.unk12);
    const negative: c_int = @as(c_int, pp.unkC) & 0x8000;
    const positive: c_int = @intFromBool(negative == 0);
    const colors = [2][4]u8{ .{ 128, 64, 0, 0 }, .{ 0, 64, 128, 0 } };
    const rgb = colors[@intCast(positive)];

    // First icon.
    {
        const cur: *v.POLY_FT4 = &prim[0];
        const r = v.g.D_80069144()[pp.unkE];
        const tag: *v.P_TAG = @ptrCast(cur);
        psx.setBits(tag, "len", 9);
        tag.code = 0x2c;
        cur.r0 = rgb[0];
        cur.g0 = rgb[1];
        cur.b0 = rgb[2];
        cur.x0 = psx.conv(i16, x);
        cur.y0 = psx.conv(i16, y);
        cur.x1 = psx.conv(i16, x +% @as(c_int, r.w));
        cur.y1 = psx.conv(i16, y);
        cur.x2 = psx.conv(i16, x);
        cur.y2 = psx.conv(i16, y +% @as(c_int, r.h));
        cur.x3 = psx.conv(i16, x +% @as(c_int, r.w));
        cur.y3 = psx.conv(i16, y +% @as(c_int, r.h));
        cur.u0 = psx.conv(u8, @as(c_int, r.x));
        cur.v0 = psx.conv(u8, @as(c_int, r.y));
        cur.u1 = psx.conv(u8, @as(c_int, r.x) +% @as(c_int, r.w));
        cur.v1 = psx.conv(u8, @as(c_int, r.y));
        cur.u2 = psx.conv(u8, @as(c_int, r.x));
        cur.v2 = psx.conv(u8, @as(c_int, r.y) +% @as(c_int, r.h));
        cur.u3 = psx.conv(u8, @as(c_int, r.x) +% @as(c_int, r.w));
        cur.v3 = psx.conv(u8, @as(c_int, r.y) +% @as(c_int, r.h));
        cur.tpage = 12; // setTPage(0, 0, 768, 0)
        cur.clut = 14325; // setClut(848, 223)
        c.AddPrim(@ptrFromInt(if (psx.get(scratch.unk4)) |q| @intFromPtr(q) +% 8 else 8), cur);
        prim += 1;
        x +%= @as(c_int, r.w) +% 2;
    }

    // Second icon.
    {
        const cur: *v.POLY_FT4 = &prim[0];
        const r = v.g.D_8006917C()[pp.unkA];
        const tag: *v.P_TAG = @ptrCast(cur);
        psx.setBits(tag, "len", 9);
        tag.code = 0x2c;
        cur.r0 = rgb[0];
        cur.g0 = rgb[1];
        cur.b0 = rgb[2];
        cur.x0 = psx.conv(i16, x);
        cur.y0 = psx.conv(i16, y);
        cur.x1 = psx.conv(i16, x +% @as(c_int, r.w));
        cur.y1 = psx.conv(i16, y);
        cur.x2 = psx.conv(i16, x);
        cur.y2 = psx.conv(i16, y +% @as(c_int, r.h));
        cur.x3 = psx.conv(i16, x +% @as(c_int, r.w));
        cur.y3 = psx.conv(i16, y +% @as(c_int, r.h));
        cur.u0 = psx.conv(u8, @as(c_int, r.x));
        cur.v0 = psx.conv(u8, @as(c_int, r.y));
        cur.u1 = psx.conv(u8, @as(c_int, r.x) +% @as(c_int, r.w));
        cur.v1 = psx.conv(u8, @as(c_int, r.y));
        cur.u2 = psx.conv(u8, @as(c_int, r.x));
        cur.v2 = psx.conv(u8, @as(c_int, r.y) +% @as(c_int, r.h));
        cur.u3 = psx.conv(u8, @as(c_int, r.x) +% @as(c_int, r.w));
        cur.v3 = psx.conv(u8, @as(c_int, r.y) +% @as(c_int, r.h));
        cur.tpage = 12; // setTPage(0, 0, 768, 0)
        cur.clut = 14325; // setClut(848, 223)
        c.AddPrim(@ptrFromInt(if (psx.get(scratch.unk4)) |q| @intFromPtr(q) +% 8 else 8), cur);
        prim += 1;
        x +%= @as(c_int, r.w) +% 2;
    }

    // Separator.
    {
        const cur: *v.POLY_FT4 = &prim[0];
        const tag: *v.P_TAG = @ptrCast(cur);
        psx.setBits(tag, "len", 9);
        tag.code = 0x2c;
        cur.r0 = rgb[0];
        cur.g0 = rgb[1];
        cur.b0 = rgb[2];
        cur.x0 = psx.conv(i16, x);
        cur.y0 = psx.conv(i16, y -% 1);
        cur.x1 = psx.conv(i16, x +% 8);
        cur.y1 = psx.conv(i16, y -% 1);
        cur.x2 = psx.conv(i16, x);
        cur.y2 = psx.conv(i16, y +% 7);
        cur.x3 = psx.conv(i16, x +% 8);
        cur.y3 = psx.conv(i16, y +% 7);
        cur.u0 = psx.conv(u8, positive *% 8 +% 120);
        cur.v0 = 208;
        cur.u1 = psx.conv(u8, positive *% 8 +% 128);
        cur.v1 = 208;
        cur.u2 = psx.conv(u8, positive *% 8 +% 120);
        cur.v2 = 216;
        cur.u3 = psx.conv(u8, positive *% 8 +% 128);
        cur.v3 = 216;
        cur.tpage = 12; // setTPage(0, 0, 768, 0)
        cur.clut = 14325; // setClut(848, 223)
        c.AddPrim(@ptrFromInt(if (psx.get(scratch.unk4)) |q| @intFromPtr(q) +% 8 else 8), cur);
        prim += 1;
    }

    x +%= 2;
    x +%= @as(c_int, pp.displayTextLen) *% 5;

    const text: [*]i8 = @ptrCast(pp);
    var i: c_int = @as(c_int, pp.displayTextLen) -% 1;
    while (i >= 0) : ({
        i -%= 1;
        x -%= 5;
    }) {
        const cur: *v.POLY_FT4 = &prim[0];
        const tag: *v.P_TAG = @ptrCast(cur);
        psx.setBits(tag, "len", 9);
        tag.code = 0x2c;
        cur.r0 = rgb[0];
        cur.g0 = rgb[1];
        cur.b0 = rgb[2];
        cur.x0 = psx.conv(i16, x);
        cur.x1 = psx.conv(i16, x +% 6);
        cur.y0 = psx.conv(i16, y -% 1);
        cur.y1 = psx.conv(i16, y -% 1);
        cur.x2 = psx.conv(i16, x);
        cur.y2 = psx.conv(i16, y +% 9);
        cur.x3 = psx.conv(i16, x +% 6);
        cur.y3 = psx.conv(i16, y +% 9);
        // The digit byte is read at p + i + 20 (the C reads past displayText's four bytes).
        const glyph: c_int = (@as(c_int, text[@as(usize, @intCast(i)) + 20]) -% '0') *% 6;
        cur.u0 = psx.conv(u8, glyph);
        cur.v0 = 0;
        cur.u1 = psx.conv(u8, glyph +% 6);
        cur.v1 = 0;
        cur.u2 = psx.conv(u8, glyph);
        cur.v2 = 10;
        cur.u3 = psx.conv(u8, glyph +% 6);
        cur.v3 = 10;
        cur.tpage = 12; // setTPage(0, 0, 768, 0)
        cur.clut = 14324; // setClut(832, 223)
        c.AddPrim(@ptrFromInt(if (psx.get(scratch.unk4)) |q| @intFromPtr(q) +% 8 else 8), cur);
        prim += 1;
    }

    psx.put(&scratch.unk0, @as(?*anyopaque, @ptrCast(prim)));
}
