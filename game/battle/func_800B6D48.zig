//! Places script text box `script[1]` at the free placement nearest the script's origin
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B6D48 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opens the text box named by the script at the first placement that fits its origin, else the nearest.
pub fn func_800B6D48(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var placements: [4]v.textPlacement = undefined;
    var origin: v.SVECTOR = undefined;
    var best: ?*v.textPlacement = null;
    var i: c_short = 0;

    const getFn = @as(*const fn (c_int) callconv(.c) c_int, @ptrCast(&c.func_800BFE50));
    const actorIndex: c_short = c.vs_battle_getShort(script + 2);
    _ = c.func_800BB7C4(getFn(@as(c_int, actorIndex)), psx.cptr(c.SVECTOR, &origin));

    origin.vz = psx.conv(i16, ((@as(c_int, origin.vz) -% 1024) & 0xC00) >> 10);

    i = 0;
    while (i < 4) : (i +%= 1) {
        const idx: usize = @intCast(i);
        if (c.func_800B6B98(psx.cptr(c.SVECTOR, &origin), script, @ptrCast(&placements[idx]), v.g.D_800E9BEC()[idx]) != 0) {
            best = &placements[idx];
            break;
        }
    }

    if (best == null) {
        best = &placements[0];
        i = 1;
        while (i < 4) : (i +%= 1) {
            const idx: usize = @intCast(i);
            if (best.?.distance > placements[idx].distance) {
                best = &placements[idx];
            }
        }
    }
    const b = best.?;

    const width: c_uint = psx.conv(c_uint, @as(c_int, script[6]) *% 12 +% 8);
    const height: c_uint = psx.conv(c_uint, @as(c_int, script[7]) *% 13 +% 4);
    const centerX: c_int = psx.conv(c_int, psx.conv(c_uint, @as(c_int, b.x)) +% (width >> 1));
    const centerY: c_int = psx.conv(c_int, psx.conv(c_uint, @as(c_int, b.y)) +% (height >> 1));

    const flags: c_short = c.vs_battle_getShort(script + 4);
    c.vs_battle_initTextBox(
        @as(c_int, script[1]),
        @as(c_int, flags),
        @as(c_int, b.x),
        @as(c_int, b.y),
        @as(c_int, script[6]),
        @as(c_int, script[7]),
        centerX,
        centerY,
    );

    const raw = c.vs_battle_getTextBox(@as(c_int, script[1])).?;
    const box: *v.vs_battle_textBox = @alignCast(@as(*align(1) v.vs_battle_textBox, @ptrCast(raw)));

    // BUG in the original: texture is never initialized; its stack value is not reproducible, so 0 here.
    const texture: c_int = 0;
    psx.setBits(&box.unk0, "unk0_24", psx.conv(u6, texture));
    psx.setBits(&box.unk0, "unk0_6", psx.getBits(&b.offset, "alignment"));
    psx.setBits(&box.unk0, "unk0_12", psx.getBits(&b.offset, "flags"));
    box.unk2E = psx.conv(i16, script[8]);
    return 0;
}
