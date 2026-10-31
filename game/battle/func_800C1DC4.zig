//! The battle target row and its leader line: the row's plate, text and
//! sweep (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C1DC4 (src/BATTLE/BATTLE.PRG/58578.c)
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
    y = @as(c_int, row.unk6) +% @as(c_int, v.g.D_800F4CB4().*);
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
    pcRow.text = psx.get(row.unk10);
    pcRow.x = @as(c_int, row.unk4);
    pcRow.y = y;
    pcRow.selected = @as(c_int, row.unk1);
    pcRow.fade = @as(c_int, row.unk2);
    pcRow.bank = @as(c_int, row.unk7);
    pcRow.slot = psx.conv(c_int, nextPrim -% psx.addr(psx.get(v.g.vs_scratch().unk4).?));
    pcRowFirst = @ptrFromInt(@as(usize, @as(*align(1) const c_uint, @ptrFromInt(0x1F800000)).*));

    c.vs_battle_renderTextRawColor(psx.get(row.unk10), textXy, color, @ptrCast(np));

    var prim: c_uint = psx.addr(psx.get(v.g.vs_scratch().unk0).?);
    if (gradient != 0) row.unk2 = psx.conv(u8, gradient -% 1);
    x = @as(c_int, row.unk4) & 0xFFFF;
    gradient = @as(c_int, @intFromBool(@as(c_int, row.unk4) < 160)) *% 2;

    const pp: [*]align(1) c_uint = @ptrFromInt(@as(usize, prim));
    pp[0] = (np[0] & 0xFFFFFF) | 0x0D000000;
    pp[1] = 0x60000000;
    pp[2] = psx.conv(c_uint, ((x +% 2 -% gradient) & 0xFFFF) | ((y +% 2) << 16));
    pp[3] = psx.conv(c_uint, (gradient +% 0x48) | 0x80000);
    pp[4] = 0xE1000200;
    pp[5] = psx.conv(c_uint, color0 | 0x38000000);
    pp[6] = psx.conv(c_uint, x | (y << 16));
    pp[7] = psx.conv(c_uint, color1);
    pp[8] = psx.conv(c_uint, ((x +% 0x48) & 0xFFFF) | (y << 16));
    pp[9] = psx.conv(c_uint, color0);
    pp[10] = psx.conv(c_uint, x | ((y +% 8) << 16));
    pp[11] = psx.conv(c_uint, color1);
    pp[12] = psx.conv(c_uint, ((x +% 0x48) & 0xFFFF) | ((y +% 8) << 16));
    pp[13] = 0xE1000000;
    np[0] = (prim << 8) >> 8;
    prim +%= 56;
    psx.put(&v.g.vs_scratch().unk0, @as(?*anyopaque, @ptrFromInt(@as(usize, prim))));

    if (psx.weak(SiteFn, "pc_classic_site")) |site| {
        site(20, &pcRow, pcRow.x, pcRow.y, pcRowFirst, @ptrFromInt(@as(usize, @as(*align(1) const c_uint, @ptrFromInt(0x1F800000)).*)));
    }

    if (row.unk1 != 0) {
        y +%= 4;
        c.func_800C02A8();
        const sv: [*]const u8 = @ptrCast(&row.unk8);
        c.MTC2(@as(*align(1) const c_uint, @ptrCast(sv)).*, 0);
        c.MTC2(@as(*align(1) const c_uint, @ptrCast(sv + 4)).*, 1);

        // PC_GTE_UNNARROWED: the projection is anchored while it runs.
        v.g.pc_gte_anchored().* +%= 1;
        _ = c.doCOP2(0x0180001);
        v.g.pc_gte_anchored().* -%= 1;

        sxy = @bitCast(c.MFC2(14));
        const otz: c_int = @bitCast(c.MFC2(7));
        _ = otz;

        var pcLine: v.pc_classic_target_line = undefined;
        pcLine.row = arg0;
        pcLine.x = @as(c_int, psx.conv(i16, sxy));
        pcLine.y = sxy >> 16;
        pcLine.row_x = @as(c_int, row.unk4);
        pcLine.row_y = y -% 4;
        pcLine.age = @as(c_int, row.unk3);
        pcRowFirst = @ptrFromInt(@as(usize, @as(*align(1) const c_uint, @ptrFromInt(0x1F800000)).*));

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
            site(21, &pcLine, pcLine.x, pcLine.y, pcRowFirst, @ptrFromInt(@as(usize, @as(*align(1) const c_uint, @ptrFromInt(0x1F800000)).*)));
        }
    }
}
