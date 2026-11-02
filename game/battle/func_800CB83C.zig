//! Draws the text boxes (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
const BubbleRect = *const fn (c_int, c_int, c_int, c_int, c_int) callconv(.c) void;
const TextBegin = *const fn () callconv(.c) void;

/// The scratchpad's packet cursor word as a guest pointer (PC_CLASSIC's first and end).
fn cursor() ?*anyopaque {
    const word: *const u32 = @ptrFromInt(0x1F800000);
    return @ptrFromInt(@as(usize, word.*));
}

/// PC_CLASSIC (classic_hook.h) around a draw: the classic-site record of what it drew.
fn classic(kind: c_int, record: ?*anyopaque, x: c_int, y: c_int, first: ?*anyopaque) void {
    if (psx.weak(Site, "pc_classic_site")) |site| site(kind, record, x, y, first, cursor());
}

/// The box's text: the typewriter once it has opened, the fixed-width font while it closes.
fn text(box: *v.vs_battle_textBox, id: c_int, state: c_int, duration: c_int, scale: c_int) void {
    if (state == duration + 1) {
        if (psx.weak(TextBegin, "pc_classic_text_begin")) |begin| begin();
        const first = cursor();
        c._printVariableWidthFont(@ptrCast(box));
        classic(18, box, id, 0, first); // PC_CLASSIC_SITE_TEXT
    } else if (state < 0) {
        c._printFixedWidthFont(@ptrCast(box), scale);
    }
}

/// orig: func_800CB83C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Opens, draws and closes the eight text boxes (back to front: their scale from their state and kind, their
/// frame and text by their kind), then links the text OT in, flips to the next of three, draws the puzzle
/// timer when shown and the rest of the overlay.
pub fn func_800CB83C() callconv(.c) void {
    var id: c_int = 7;
    while (id != -1) : (id -= 1) {
        const box = &v.g.textBoxes()[@intCast(id)];
        var state: c_int = box.state;
        if (state == 0) {
            continue;
        }
        const flags: c_int = @as(*align(4) const c_int, @ptrCast(&box.unk0)).*;
        v.g.D_800F4E80().* = @intCast(id);
        if (flags < 0) {
            continue;
        }
        const duration: c_int = box.unk22;
        var scale: c_int = v.g.vs_gametime_tickspeed().* >> 1;
        if (state < 0) {
            state -%= scale;
            if (state < -duration) {
                box.state = 0;
                continue;
            }
            scale = @divTrunc((duration +% state +% 1) << 16, duration);
        } else {
            state +%= scale;
            if (duration < state) {
                state = duration + 1;
            }
            scale = state -% 1;
            var settle = false; // the C's `goto settle`
            switch ((@as(c_uint, @bitCast(flags)) >> 4) & 3) {
                0, 1 => scale = c.rsin(@divTrunc(scale << 10, duration)) *% 16,
                2 => {
                    if (scale < duration - 1) {
                        scale = @divTrunc(scale << 16, duration - 2);
                    } else {
                        settle = true;
                    }
                },
                3 => {
                    if (duration >= 3) {
                        if (scale < duration - 1) {
                            scale = @divTrunc(scale *% 0xC000, duration - 2) +% 0x6000;
                        } else {
                            settle = true;
                        }
                    }
                },
                else => unreachable, // masked to 0..3
            }
            if (settle) {
                scale = if (scale == duration) 0x10000 else 0x11000;
            }
        }
        const x: c_int = @as(c_int, box.x) *% scale;
        const reverse: c_int = 0x10000 -% scale;
        const cx: c_int = @as(c_int, box.centerX) *% reverse;
        const y: c_int = @as(c_int, box.y) *% scale;
        const cy: c_int = @as(c_int, box.centerY) *% reverse;
        const width: c_int = (@as(c_int, box.charsPerLine) * 12 + 10) *% scale;
        const height: c_int = (@as(c_int, box.lineCount) * 13 + 4) *% scale;
        box.state = psx.conv(i16, state);
        // ROOD_PC: the talk bubble's rectangle.
        if (psx.weak(BubbleRect, "pc_bubble_rect")) |rect| rect(id, x +% cx, y +% cy, width, height);
        box.unk24 = psx.conv(i16, (x +% cx) >> 16);
        box.unk26 = psx.conv(i16, (y +% cy) >> 16);
        box.unk28 = psx.conv(i16, width >> 16);
        box.unk2A = psx.conv(i16, height >> 16);
        switch (psx.getBits(&box.unk0, "unk0_0")) {
            0 => {
                if (state < 0) {
                    c._renderTextImmediate(@ptrCast(box), scale);
                } else {
                    text(box, id, state, duration, scale);
                }
            },
            4 => {
                const first = cursor();
                c.func_800CD158(id);
                classic(17, box, id, 0, first); // PC_CLASSIC_SITE_FADE_EDGE
                if (state >= 0) {
                    text(box, id, state, duration, scale);
                }
            },
            6 => c._renderTextImmediate(@ptrCast(box), scale),
            2, 7 => {
                const first = cursor();
                c.func_800CD3E4(id);
                classic(16, box, id, 0, first); // PC_CLASSIC_SITE_WINDOW
                text(box, id, state, duration, scale);
            },
            3 => {},
            else => text(box, id, state, duration, scale),
        }
    }
    const ot_base = v.g.D_800F51B8();
    const ot_address: usize = if (ot_base.*) |p| @intFromPtr(p) else 0;
    c.AddPrims(@ptrFromInt(@as(usize, psx.rawOf(v.g.vs_scratch().unk4)) -% 6 * 4), @ptrFromInt(ot_address), @ptrFromInt(ot_address +% 33 * 4));
    const page = v.g.D_800EB9CE();
    page.* = if (page.* == 2) 0 else page.* +% 1;
    ot_base.* = @as([*]u32, @ptrCast(v.g.D_800F4CD0())) + @as(usize, page.*) * 34;
    _ = c.ClearOTag(@ptrCast(ot_base.*), 34);
    if (v.g.vs_battle_lowerScreenUiState().* == 1) {
        const first = cursor();
        c._renderTimer(v.g.D_8005046C());
        classic(14, @ptrCast(v.g.D_800EC258().*), 128, 200, first); // PC_CLASSIC_SITE_TIMER
    }
    c.func_800CB708();
    _ = c.vs_battle_keystreamBits(0);
}
