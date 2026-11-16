//! Draws one of the loaded GIM images (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_render_drawGimLayer (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Image `index` of vs_render_gimLayers, unless hidden (brightness 0, mode other than 2): its CLUT from its palette and flags, drawn by its mode's drawer (vs_render_gimLayerDrawRoutines) into the front or the menu OT. ROOD_PC: the classic-site record around the draw, and the opening's ready hook.
pub fn vs_render_drawGimLayer(index: c_int) callconv(.c) void {
    const Drawer = *const fn (?*v.gim_t, c_int, [*c]c_uint) callconv(.c) void;
    const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
    const gim = psx.at(v.g.vs_render_gimLayers().*.?, index);
    var clut: c_int = (@as(c_int, gim.unk2) - 16) << 6;
    if (gim.unk3 == 0 and psx.getBits(gim, "unk0_3") != 2) {
        return;
    }
    if (psx.getBits(gim, "unk0_2") != 0) {
        if (psx.getBits(gim, "unk0_12") != 0) {
            clut = (511 << 6) | ((clut >> 4) & 0x3F);
        } else {
            clut = (227 << 6) | ((768 >> 4) & 0x3F);
        }
    } else {
        clut = (511 << 6) | (((clut +% index *% 16) >> 4) & 0x3F);
    }
    v.g.D_800F51C8().* = 0;
    // ROOD_PC: PC_CLASSIC (classic_hook.h) around the draw: the image, its drawer, CLUT and slot.
    const scratch = v.g.vs_scratch();
    const ot_address: usize = if ((gim.unkC & 0x4000) != 0)
        @intFromPtr(psx.get(scratch.unk4).?) +% @as(usize, @bitCast(@as(isize, 0x1FF8 - @as(isize, index) * 4)))
    else
        @intFromPtr(psx.get(scratch.unk8).?) +% @as(usize, @bitCast(@as(isize, 0x18 - @as(isize, index) * 8)));
    const ot: [*c]c_uint = @ptrFromInt(ot_address);
    var record = c.pc_classic_image{
        .image = index,
        .mode = psx.getBits(gim, "unk0_3"),
        .clut = clut << 16,
        .slot = @truncate(@as(isize, @bitCast(ot_address -% @intFromPtr(psx.get(scratch.unk4).?)))),
        .gim = gim,
    };
    const first: ?*anyopaque = v.g.vs_scratch().unk0;
    const drawer: ?Drawer = @ptrCast(psx.at(v.g.vs_render_gimLayerDrawRoutines(), psx.getBits(gim, "unk0_3")).*); // vs_render_gimLayerDrawRoutines[4]: the 3-bit mode is not checked
    drawer.?(gim, clut << 16, ot);
    if (psx.weak(Site, "pc_classic_site")) |site| site(19, &record, index, 0, first, v.g.vs_scratch().unk0); // PC_CLASSIC_SITE_IMAGE
    // ROOD_PC: the opening's first image is ready.
    if (index == 0 and gim.unk3 == 0x80) {
        c.pc_opening_image_ready(gim.id);
    }
}
