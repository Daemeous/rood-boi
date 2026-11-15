//! The battle menu item renderer: its label, its gradient background quad and its sub text
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

/// orig: vs_battle_renderMenuItem (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws a menu item's label, gradient background and sub text into the packet at the scratchpad cursor.
pub fn renderMenuItem(menuItem: ?*v.vs_battle_menuItem_t) callconv(.c) void {
    const m = menuItem.?;
    var y: c_int = m.gradientState;
    const scratch: [*c]c_uint = @ptrFromInt(@intFromPtr(v.g.vs_scratch().unk8) +% 8);
    const w: c_int = m.w;
    const bgWidth: c_int = m.backgroundWidth;
    const text: [*]u8 = &m.text;

    if (m.state != 0) {
        if ((m.selected | m.invertGradient) != 0 and m.unselectable == 0) {
            y = 8;
        }

        var color: c_int = m.fontColor;
        if (color == 0) {
            color = @as(c_int, m.unselectable) * 3;
        }

        c.vs_battle_setFontStyle(color +% 4);

        const tempX: c_int = m.x;
        var penX: c_int = tempX +% 6;
        if (psx.conv(c_uint, @as(c_int, m.rowIcon) -% 1) < 23) {
            penX = tempX +% 22;
        }

        var ch: c_int = text[0];
        var pText: [*]u8 = text + 1;
        while (ch != 0xFF) {
            if (ch == c.vs_char_spacing) {
                penX +%= @as(c_int, pText[0]);
                pText += 1;
            } else {
                penX = c.vs_battle_printVariableWidthFontChar(psx.conv(c_uint, ch), penX, m.y, scratch - 3);
            }
            ch = @as(c_int, pText[0]);
            pText += 1;
        }

        const topColor: c_int = c.vs_battle_uiGradientStop(psx.conv(c_uint, 8 -% y), psx.conv(c_uint, bgWidth), 128);
        const bottomColor: c_int = c.vs_battle_uiGradientStop(psx.conv(c_uint, y), psx.conv(c_uint, bgWidth), 128);

        if ((bgWidth & 7) != 0) {
            m.backgroundWidth = psx.conv(u8, (bgWidth +% 1) & 0xF);
        }

        if (y != 0) {
            m.gradientState = psx.conv(u8, y -% 1);
        }

        // The C reads x and y as one int: x is the low half (zero-extended), y the high half (sign-extended).
        const x: c_int = @as(u16, @bitCast(m.x));
        y = m.y;

        sprite_item.tileFlat(
            scratch + 1,
            false,
            0,
            @bitCast((bits(x +% 2) & 0xFFFF) | (bits(y +% 2) << 16)),
            w | 0xC0000,
        );

        // The row's gradient: a shaded quad, dithered on for it and off after, as a draw item at the row's slot.
        const rgb = [4]c_int{ topColor & 0xFFFFFF, bottomColor & 0xFFFFFF, topColor & 0xFFFFFF, bottomColor & 0xFFFFFF };
        const corners = [4]c_int{
            @bitCast(bits(x) | (bits(y) << 16)),
            @bitCast(bits((x +% w) & 0xFFFF) | (bits(y) << 16)),
            @bitCast(bits(x) | (bits(y +% 12) << 16)),
            @bitCast(bits((x +% w) & 0xFFFF) | (bits(y +% 12) << 16)),
        };
        sprite_item.g4After(scratch, @bitCast(tpage(c.ditheringOn)), @bitCast(tpage(c.ditheringOff)), &rgb, &corners);

        if (psx.get(m.subText)) |subText| {
            c.vs_battle_renderTextRaw(
                subText,
                @bitCast(bits(x -% 10) | (bits(y +% 1) << 16)),
                @ptrCast(scratch),
            );
        }
    }
}

/// The bit pattern of a C int.
fn bits(value: c_int) c_uint {
    return @bitCast(value);
}

/// getTpage(0, 0, clut4Bit, semiTransparencyHalf, dithering): the macro's x and y terms are zero (the draw mode's bits; the item
/// takes those and no GP0 code).
fn tpage(dithering: c_int) c_uint {
    return (@as(c_uint, @intCast(dithering & 1)) << 9) |
        ((@as(c_uint, @intCast(c.clut4Bit)) & 3) << 7) |
        ((@as(c_uint, @intCast(c.semiTransparencyHalf)) & 3) << 5);
}
