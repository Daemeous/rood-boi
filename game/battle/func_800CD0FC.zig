//! Sets the default sprite for slot `arg1` and patches word 4 of its result
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

const Count = *const fn () callconv(.c) c_int;
const MenuOpen = *const fn () callconv(.c) c_int;
const Anchor = *const fn (c_int, c_int, c_int) callconv(.c) void;
/// PC_ANCHOR_LEFT_EDGE (pc/src/widescreen.h).
const anchor_left_edge: c_int = 0x11;

/// orig: func_800CD0FC (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the default sprite of slot `arg1` (below 3) and stores a table word at its index 4.
pub fn func_800CD0FC(arg0: c_int, arg1: c_uint) callconv(.c) void {
    if (arg1 < 3) {
        // The right-hand table load happens before the call: the call may change
        // the table's bytes, and the object reads the value first.
        const value = psx.conv(c_uint, v.g.D_800EC2D8()[arg1]);
        // ROOD_PC: the header (CAUTION, INFORMATION, CHECK) sits at the box's left corner: the box's tile is widened to the 16:9 view, so
        // the header goes to the view's left edge with it, not left inside the 4:3 screen.
        // A menu's information box keeps its label above its text (the menus' 16:9 layout stays in the 4:3 screen, the text under the label
        // where the original puts it): only the field's message box, whose text is centred, takes the header to the view's edge.
        const in_menu = if (psx.weak(MenuOpen, "pc_battle_field_menu_open")) |open| open() != 0 else false;
        const first: c_int = if (psx.weak(Count, "pc_frame_count")) |count| count() else 0;
        defer if (!in_menu) {
            if (psx.weak(Anchor, "pc_frame_anchor")) |anchor| anchor(first, psx.weak(Count, "pc_frame_count").?(), anchor_left_edge);
        };
        _ = sprite_item.sprite(null, 0x80, (@as(c_int, arg0) << 16) | 0xA, v.g.D_800EC2CC()[arg1], sprite_item.page, @bitCast(value));
    }
}
