//! Draws a text box's fading edge (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

/// orig: func_800CD158 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// The 4..16 lines above a top box (below a bottom one) as two sprites each, fading out line by line; the rest of the screen past them a dark tile; a bottom box at its last state also draws its arrow (func_800CD0FC).
pub fn func_800CD158(id: c_int) callconv(.c) void {
    const box = psx.at(v.g.textBoxes(), id);
    var y: c_int = box.y;
    const ot: [*c]c_uint = @ptrCast(psx.at(v.g.vs_menu_activeTextBoxOrderingTable().*.?, id *% 4 +% 1));
    const edge: c_int = box.unk2A;
    const offset: c_int = if (v.g.vs_main_frameBuf().* != 0) 0 else 320;
    if (y == 0) {
        y = edge;
    } else {
        y = 240 -% edge;
    }
    var count: c_uint = @bitCast(@as(c_int, box.lineCount) * 13 + 4 -% edge);
    if (count > 16) {
        count = 16;
    } else if (count < 4) {
        count = 4;
    }
    var i: c_uint = 0;
    while (i < count) : (i += 1) { // i < count <= 16
        var brightness: c_uint = ((count - i) * 128) / (count + 1);
        const page: c_uint = (@as(c_uint, @bitCast(offset + 256)) >> 6) | 0x100;
        _ = sprite_item.sprite(ot, @bitCast(brightness), (y << 16) | 256, 0x10040, @bitCast(page | 0xE1000000), y << 8);
        const page2: c_uint = (@as(c_uint, @bitCast(offset)) >> 6) | 0x100;
        _ = sprite_item.sprite(ot, @bitCast(brightness), y << 16, 0x10100, @bitCast(page2 | 0xE1000000), y << 8);
        brightness = page2;
        if (y < 128) {
            y -%= 1;
            if (y < 0) {
                break;
            }
        } else {
            y +%= 1;
            if (y >= 240) {
                break;
            }
        }
    }
    if (y < 128) {
        if (y >= 0) {
            sprite_item.tileFlat(ot, false, 0, 0, ((y +% 1) << 16) | 320);
        }
    } else {
        if (y < 240) {
            sprite_item.tileFlat(ot, false, 0, y << 16, (0xF00000 -% (y << 16)) | 320);
        }
        if (box.state == @as(c_int, box.unk22) + 1) {
            c.func_800CD0FC(y -% 7, @bitCast(@as(c_int, psx.getBits(&box.flags, "unk0_8")) - 1));
        }
    }
}
