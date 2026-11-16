//! The battle target row and its leader line: the row's plate, text and
//! sweep (pc/src/game/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");
const sprite_item = common.sprite_item;

/// orig: func_800C1DC4 (pc/src/game/BATTLE/BATTLE.PRG/58578.c)
/// Draws target row `arg0`: its gradient plate and text, then its leader line to the projected point when selected.
pub fn func_800C1DC4(arg0: ?*v.D_800EB9B8_unk990) callconv(.c) void {
    const row = arg0.?;
    const SiteFn = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;

    var sxy: c_int = undefined;
    var gradient: c_int = @as(c_int, row.unk2);
    var color0: c_int = undefined;
    var color1: c_int = undefined;
    var x: c_int = undefined;
    var y: c_int = undefined;
    var xy: c_int = undefined;
    var ringXy: c_int = undefined;
    var textXy: c_int = undefined;
    var color: c_int = undefined;
    var pcRowFirst: ?*anyopaque = undefined;

    // vs_scratch.unk8 is a void*: the subtraction below is in bytes, as GNU C does.
    const scratchUnk8: c_uint = psx.addr(psx.get(v.g.vs_scratch().unk8).?);
    const nOff: c_int = @as(c_int, row.unk1) *% 4 -% 8;
    const nextPrim: c_uint = scratchUnk8 -% @as(c_uint, @bitCast(nOff));
    const np: [*]align(1) c_uint = @ptrFromInt(@as(usize, nextPrim));

    if (row.unk1 != 0) gradient = 8;
    color0 = 8 -% gradient;
    const colorIndex: c_uint = psx.conv(c_uint, @as(c_int, row.unk7) *% 8);
    color0 = c.vs_battle_uiGradientStop(psx.conv(c_uint, color0), colorIndex, 0x80);
    color1 = c.vs_battle_uiGradientStop(psx.conv(c_uint, gradient), colorIndex, 0x80);

    xy = @as(c_int, row.unk4);
    const isLeft = xy < 160;
    if (isLeft) {
        y = color0;
        color0 = color1;
        color1 = y;
    }
    y = @as(c_int, row.unk6) +% @as(c_int, v.g.vs_battle_targetColumnTopOffset().*);
    x = xy;
    if (isLeft) {
        x +%= 0x42;
    } else {
        x +%= 6;
    }
    color = 0x404040;
    textXy = (x & 0xFFFF) | ((y -% 1) << 16);
    if (row.unk1 != 0) color = 0x808080;

    var pcRow: v.pc_classic_target_row = undefined;
    pcRow.row = arg0;
    pcRow.text = row.limbName;
    pcRow.x = @as(c_int, row.unk4);
    pcRow.y = y;
    pcRow.selected = @as(c_int, row.unk1);
    pcRow.fade = @as(c_int, row.unk2);
    pcRow.bank = @as(c_int, row.unk7);
    pcRow.slot = psx.conv(c_int, nextPrim -% psx.addr(psx.get(v.g.vs_scratch().unk4).?));
    pcRowFirst = v.g.vs_scratch().unk0;

    c.vs_battle_renderTextRawColor(row.limbName, textXy, color, @ptrCast(np));

    if (gradient != 0) row.unk2 = psx.conv(u8, gradient -% 1);
    x = @as(c_int, row.unk4) & 0xFFFF;
    gradient = @as(c_int, @intFromBool(@as(c_int, row.unk4) < 160)) *% 2;

    // The row's plate as draw items at its slot: a black tile (the draw mode the GPU is in), then in the dithered mode a gradient quad
    // that leaves mode 0 set (the packet's four parts, in the order they draw: the last added draws first).
    sprite_item.drawMode(@ptrCast(np), @bitCast(@as(c_uint, 0xE1000000)));
    sprite_item.shaded(@ptrCast(np), 4, false, &[4]c_int{ color0, color1, color0, color1 }, &[4]c_int{
        x | (y << 16),
        ((x +% 0x48) & 0xFFFF) | (y << 16),
        x | ((y +% 8) << 16),
        ((x +% 0x48) & 0xFFFF) | ((y +% 8) << 16),
    });
    sprite_item.drawMode(@ptrCast(np), @bitCast(@as(c_uint, 0xE1000200)));
    sprite_item.tileFlat(@ptrCast(np), false, 0, ((x +% 2 -% gradient) & 0xFFFF) | ((y +% 2) << 16), (gradient +% 0x48) | 0x80000);

    if (psx.weak(SiteFn, "pc_classic_site")) |site| {
        site(20, &pcRow, pcRow.x, pcRow.y, pcRowFirst, v.g.vs_scratch().unk0);
    }

    if (row.unk1 != 0) {
        y +%= 4;
        c.vs_render_loadFrameViewMatrix();
        const frame = common.gte.frame();

        // PC_GTE_UNNARROWED: the projection is anchored while it runs.
        v.g.pc_gte_anchored().* +%= 1;
        const projected = common.gte.project(&frame, &row.unk8);
        v.g.pc_gte_anchored().* -%= 1;

        sxy = projected.sxy;

        var pcLine: v.pc_classic_target_line = undefined;
        pcLine.row = arg0;
        pcLine.x = @as(c_int, psx.conv(i16, sxy));
        pcLine.y = sxy >> 16;
        pcLine.row_x = @as(c_int, row.unk4);
        pcLine.row_y = y -% 4;
        pcLine.age = @as(c_int, row.unk3);
        pcRowFirst = v.g.vs_scratch().unk0;

        if (@as(c_int, row.unk4) < 160) {
            x = (x +% 0x47) & 0xFFFF;
            ringXy = x | (y << 16);
            c.func_800C1A40(sxy, ringXy, @as(c_int, row.unk3) -% 2);
            if (@as(c_int, row.unk3) >= 18) {
                c.func_800C1A40(sxy, ringXy, (@as(c_int, row.unk3) +% 14) & 0x1F);
            }
        } else {
            ringXy = x | (y << 16);
            c.func_800C16FC(sxy, ringXy, @as(c_int, row.unk3) -% 2);
            if (@as(c_int, row.unk3) >= 18) {
                c.func_800C16FC(sxy, ringXy, (@as(c_int, row.unk3) +% 14) & 0x1F);
            }
        }

        if (psx.weak(SiteFn, "pc_classic_site")) |site| {
            site(21, &pcLine, pcLine.x, pcLine.y, pcRowFirst, v.g.vs_scratch().unk0);
        }
    }
}
