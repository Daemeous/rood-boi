//! Lets the player pick one of the shown text boxes (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_selectTextBox (src/BATTLE/BATTLE.PRG/5BF94.c)
/// First call: orders the boxes in `box_mask` by height and starts on the state flags' choice; later calls move with up / down, confirm with any face or shoulder button (returns the choice + 1). The chosen box is drawn bright and swapped to the front; 0 while choosing.
pub fn selectTextBox(box_mask: u8) callconv(.c) c_int {
    const selector = v.g.textBoxSelector();
    const boxes = v.g.textBoxes();
    var y_positions: [8]i16 = undefined;
    var selected: c_int = undefined;
    var count: c_uint = 0;
    if (selector.count == 0) {
        var i: u4 = 0;
        while (i < 8) : (i += 1) { // i < 8
            y_positions[i] = 0x7FFF;
            if (((@as(c_int, box_mask) >> @as(u5, i)) & 1) != 0) {
                count += 1;
                y_positions[i] = boxes[i].y;
            }
        }
        var k: c_uint = 0;
        while (k < count) : (k += 1) { // k < count <= 8
            var sorted: usize = 0;
            var j: usize = 0;
            while (j < 8) : (j += 1) {
                if (y_positions[sorted] > y_positions[j]) {
                    sorted = j;
                }
            }
            selector.order[k] = @intCast(sorted);
            y_positions[sorted] = 0x7FFF;
        }
        selected = @as(c_int, v.g.vs_main_stateFlags().unk11) - 1;
        if (psx.conv(c_uint, selected) >= count) { // unsigned, as the C compares it
            selected = 0;
        }
        selector.selectedIndex = 0;
        selector.count = psx.conv(u8, count);
    } else {
        selected = selector.selectedIndex;
        count = selector.count;
        const buttons = v.g.vs_main_buttonsPressed().all;
        if ((buttons & 0xFF) != 0) {
            c.vs_battle_playMenuSelectSfx();
            selector.count = 0;
            return selected + 1;
        }
        if ((buttons & (1 << 12)) != 0) {
            const previous = selected - 1;
            selected = @bitCast(psx.conv(c_uint, previous) +% count);
        }
        if ((buttons & (1 << 14)) != 0) {
            selected += 1;
        }
        selected = @bitCast(psx.conv(c_uint, selected) % count); // unsigned, as the C's u_int count makes it
    }
    if (selected != selector.selectedIndex) {
        c.vs_battle_playMenuChangeSfx();
        selector.selectedIndex = psx.conv(u8, selected);
    }
    var j: c_int = selected;
    var i: c_uint = 0;
    while (i < count) : (i += 1) {
        if (psx.at(&selector.order, j).* < selector.order[i]) {
            j = @intCast(i);
        }
    }
    const temp_order = psx.at(&selector.order, selected).*;
    psx.at(&selector.order, selected).* = psx.at(&selector.order, j).*;
    psx.at(&selector.order, j).* = temp_order;
    const temp_box = boxes[temp_order];
    boxes[temp_order] = boxes[psx.at(&selector.order, selected).*];
    boxes[psx.at(&selector.order, selected).*] = temp_box;
    i = 0;
    while (i < count) : (i += 1) {
        boxes[selector.order[i]].brightness = if (i == psx.conv(c_uint, selected)) 0x80 else 0x40;
    }
    return 0;
}
