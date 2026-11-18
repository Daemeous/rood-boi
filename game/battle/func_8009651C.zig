//! Draws the shadow quad under a unit: a textured quad around its position,
//! one of eight corners picked by facing (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const sprite_item = common.sprite_item;

fn xy(x: c_int, y: c_int) c_int {
    return @bitCast((@as(c_uint, @bitCast(x)) & 0xFFFF) | (@as(c_uint, @bitCast(y)) << 16));
}

/// orig: func_8009651C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Adds the shadow quad at `position` for `direction`: a draw item (texels as they are, page 768 and the CLUT at 976, 223) at the
/// slot 16 bytes before the table's scene base.
pub fn func_8009651C(position: ?*v.SVECTOR, direction: c_int) callconv(.c) void {
    const dx = [8]c_int{ -8, 0, 7, 10, 7, 0, -8, -11 };
    const dy = [8]c_int{ -8, -11, -8, 0, 7, 10, 7, 0 };
    // The C reads the position before the direction test, so a null position faults here too.
    const x: c_int = position.?.vx;
    const y: c_int = position.?.vy;
    const scratch = common.scratch.get();

    var index: usize = switch (direction) {
        4 => 0,
        6 => 1,
        2 => 2,
        10 => 3,
        8 => 4,
        9 => 5,
        1 => 6,
        5 => 7,
        else => return,
    };

    var corners: [4]c_int = undefined;
    corners[0] = xy(dx[index] +% x, dy[index] +% y);
    index = (index + 2) & 7;
    corners[1] = xy(dx[index] +% x, dy[index] +% y);
    index = (index + 2) & 7;
    corners[3] = xy(dx[index] +% x, dy[index] +% y);
    index = (index + 2) & 7;
    corners[2] = xy(dx[index] +% x, dy[index] +% y);
    // setUV4(48, 48, 63, 48, 48, 63, 63, 63)
    const texels = [4]c_int{ 48 | (48 << 8), 63 | (48 << 8), 48 | (63 << 8), 63 | (63 << 8) };
    // AddPrim(psx_get(unk4) - 16, ...): the ordering table address is a guest address, 16 bytes back.
    const ot_addr: usize = @intFromPtr(psx.get(scratch.unk4).?) -% 16;
    // (the packet's colour was never set: a shade-tex polygon ignores it)
    _ = sprite_item.ft4(@ptrFromInt(ot_addr), true, false, (768 & 0x3ff) >> 6, (223 << 6) | ((976 >> 4) & 0x3f), 0, &corners, &texels);
}
