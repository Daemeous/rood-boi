//! The target marker drawn at a 3D position (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const gte = common.gte;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C28AC (src/BATTLE/BATTLE.PRG/58578.c)
/// Projects `position` to the screen and links the target marker's sprites into the ordering table; `arg1` selects the marker half (0 links the frame primitives too).
pub fn func_800C28AC(position: ?*v.SVECTOR, arg1: c_int) callconv(.c) void {
    // psx_get yields a void*: the C steps back 4 bytes, not 4 words.
    const ot = psx.byteAt(c_uint, psx.get(v.g.vs_scratch().unk4).?, -4);

    var pcMarker: v.pc_classic_target_marker = undefined;

    gte.ldv0(position.?);
    // PC_GTE_UNNARROWED: widescreen keeps the marker 2D.
    v.g.pc_gte_anchored().* +%= 1;
    gte.rtps2();
    v.g.pc_gte_anchored().* -%= 1;

    var xy: c_int = undefined;
    gte.stsxy(&xy);
    const xyu: c_uint = @bitCast(xy);

    const step = (@as(c_int, v.g.D_800EA6AC().*) +% 1) & 0xF;
    v.g.D_800EA6AC().* = psx.conv(u8, step);

    pcMarker.x = @as(c_int, psx.conv(i16, xy));
    pcMarker.y = xy >> 16;
    pcMarker.lock = arg1;
    pcMarker.step = @as(c_int, v.g.D_800EA6AC().*);
    const pcMarkerFirst: u32 = @as(*const u32, @ptrFromInt(0x1F800000)).*;

    if (arg1 == 0) {
        var prim: [*]align(1) c_uint = @ptrCast(psx.get(v.g.vs_scratch().unk0).?);
        prim[0] = (ot.* & 0xFFFFFF) | 0x9000000;
        prim[1] = 0x2C808080;
        prim[2] = xyu +% 0xFFF7FFF3;
        prim[3] = 0x37FD38F1;
        prim[4] = xyu +% 0xFFF7FFFD;
        prim[5] = 0xC38E7;
        prim[6] = xyu +% 0xFFFEFFF3;
        prim[7] = 0x3FF1;
        prim[8] = xyu +% 0xFFFEFFFD;
        prim[9] = 0x3FE7;
        ot.* = psx.addr(prim);
        prim += 10;
        prim[0] = (ot.* & 0xFFFFFF) | 0x9000000;
        prim[1] = 0x2C808080;
        prim[2] = xyu +% 0x1FFF3;
        prim[3] = 0x37FD40F1;
        prim[4] = xyu +% 0x1FFFD;
        prim[5] = 0xC40E7;
        prim[6] = xyu +% 0x8FFF3;
        prim[7] = 0x47F1;
        prim[8] = xyu +% 0x8FFFD;
        prim[9] = 0x47E7;
        ot.* = psx.addr(prim);
        prim += 10;
        psx.put(&v.g.vs_scratch().unk0, prim);
    }

    var i: c_int = 0;
    while (i < 3) : (i +%= 1) {
        const index: c_int = i +% arg1 *% 2;
        const brightness: c_int = if (index == 2)
            @as(c_int, v.g.vs_battle_cursorBrightnessAnimation()[v.g.D_800EA6AC().*])
        else
            0x80;
        const otPtr: [*c]c_uint = @ptrFromInt(@as(usize, psx.addr(ot)));
        const prim: [*c]c_uint = c.vs_battle_setSpriteDefaultTexPage(
            brightness,
            xy +% psx.at(v.g.D_800EA670(), index).*,
            psx.at(v.g.D_800EA684(), index).*,
            otPtr,
        );
        prim[4] = psx.conv(c_uint, psx.at(v.g.D_800EA698(), index).*);
    }

    // ROOD_PC: PC_CLASSIC target marker site (classic_hook.h); weak, tested for presence.
    const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
    if (psx.weak(Site, "pc_classic_site")) |site| {
        site(
            22,
            &pcMarker,
            pcMarker.x,
            pcMarker.y,
            @ptrFromInt(@as(usize, pcMarkerFirst)),
            @ptrFromInt(@as(usize, @as(*const u32, @ptrFromInt(0x1F800000)).*)),
        );
    }
}
