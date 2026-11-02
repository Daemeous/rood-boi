//! zig_diff spec: _renderTextImmediate -> render_text_immediate.renderTextImmediate (5BF94.c).
//! Inputs: a text box (x, y near the screen's edges or inside it; 0..8
//! characters per line, 0..6 lines: the area's copy fits the arena; the
//! indents near or past the box's area) whose string, in the arena, is up
//! to 40 tokens of glyphs (0..228 but 143), spaces (143), the other control
//! codes 229..235 (232 a newline), the move opcode 0xFA (its byte >= 0xF0 or
//! not), the font/justify/line-feed opcode 0xFB (5, 6, 4, other low values,
//! line feeds), other opcodes, then the end 231. The opacity arg1 is below
//! or above 1.0. pc_hw_opacity_on's stub returns 0 or 1 (the image path or
//! the PC's glyph path, whose state is the file statics _pcFadeOt,
//! _pcFadeStart, _pcFadeColor, _pcFadeBrightness: globalised, randomised
//! here); vs_main_allocHeapR hands out an arena block; the line length and
//! the glyph printer return any value.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").renderTextImmediate));

pub const classes = [_][:0]const u8{ "offscreen", "glyphs", "image" };

var glyphs = false;

pub const stubs = struct {
    pub fn pc_hw_opacity_on(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return @intFromBool(glyphs);
    }

    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        return @ptrCast(ctx.allocBytes(@intCast(size), 16));
    }
};

fn token(ctx: *zd.Ctx, out: [*]u8) usize {
    switch (ctx.range(u8, 0, 9)) {
        0, 1, 2 => {
            var g = ctx.range(u8, 0, 228);
            if (g == 143) g = 0;
            out[0] = g;
            return 1;
        },
        3 => {
            out[0] = 143;
            return 1;
        },
        4 => {
            out[0] = ctx.edge(u8, &.{ 229, 230, 232, 232, 233, 234, 235 });
            return 1;
        },
        5 => {
            out[0] = 0xFA;
            out[1] = ctx.pick(u8, &.{ 0xF0, 0xEF, 0xFF, 0, 1, 12 });
            return 2;
        },
        6, 7 => {
            out[0] = 0xFB;
            out[1] = ctx.edge(u8, &.{ 4, 4, 5, 6, 0, 1, 2, 3, 7, 8, 0x0D, 0x68, 0xF8 });
            return 2;
        },
        else => {
            out[0] = ctx.edge(u8, &.{ 0xEC, 0xF8, 0xF9, 0xFC, 0xFF });
            out[1] = ctx.int(u8);
            return 2;
        },
    }
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const box = ctx.alloc(v.vs_battle_textBox);
    const string = ctx.allocBytes(128, 4);
    var n: usize = 0;
    const tokens = ctx.range(usize, 0, 40);
    for (0..tokens) |_| n += token(ctx, string + n);
    string[n] = 231;
    psx.put(&box.string, @as(*u8, @ptrCast(string)));

    box.charsPerLine = ctx.range(u8, 0, 8);
    box.lineCount = ctx.range(u8, 0, 6);
    const width = @as(i16, box.charsPerLine) * 12 + 10;
    const height = @as(i16, box.lineCount) * 13 + 4;
    box.x = if (ctx.chance(6)) ctx.pick(i16, &.{ -1, 0, 320 - width, 319 - width }) else ctx.range(i16, 0, 200);
    box.y = if (ctx.chance(6)) ctx.pick(i16, &.{ -1, 0, 224 - height, 223 - height }) else ctx.range(i16, 0, 130);
    box.xIndent = box.x +% ctx.pick(i16, &.{ 0, 2, -1, width - 12, width - 11 });
    box.yIndent = box.y +% ctx.pick(i16, &.{ 0, 2, -1, height - 12, height - 11 });

    glyphs = ctx.chance(2);
    v.g.vs_main_frameBuf().* = ctx.edge(i32, &.{ 0, 1 });
    v.g.D_800F4E80().* = ctx.range(u8, 0, 1);
    const ot = ctx.allocBytes(64, 16);
    v.g.D_800F51B8().* = @ptrCast(@alignCast(ot));
    v.g._fontTable().* = ctx.int(u8);
    v.g.vs_main_buttonsPressed().all = if (ctx.chance(2)) 0x20 else ctx.int(u32) & ~@as(u32, 0x20);
    v.g._pcFadeOt().* = @ptrFromInt(ctx.range(usize, 0x1000, 0xFFFF_0000) & ~@as(usize, 3));
    v.g._pcFadeStart().* = @ptrFromInt(ctx.range(usize, 0x1000, 0xFFFF_0000));
    v.g._pcFadeColor().* = ctx.int(c_int);
    v.g._pcFadeBrightness().* = ctx.int(c_int);
    const arg1 = ctx.pick(c_int, &.{ 0, 0x8000, 0xFFFF, 0x10000, 0x10001, -1 });
    return .{ box, arg1 };
}

pub fn classify(args: Args) usize {
    const box = args[0].?;
    const x: c_int = box.x;
    const y: c_int = box.y;
    const width = @as(c_int, box.charsPerLine) * 12 + 10;
    const height = @as(c_int, box.lineCount) * 13 + 4;
    if (x < 0 or y < 0 or x + width >= 320 or y + height >= 224) return 0;
    return if (glyphs) 1 else 2;
}
