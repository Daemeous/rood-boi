//! Records the door entered and marks it in the room's door list (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// `p + k` as the C computes it: a signed int offset in ints, unchecked.
fn skip(p: [*]i32, k: c_int) [*]i32 {
    return @ptrCast(psx.at(p, k));
}

/// orig: vs_battle_setDoorEntered2 (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets vs_battle_doorEntered to `door` and sets bit 0x10 of byte 2 of that door's word in section F's door list.
pub fn setDoorEntered2(door: c_int) callconv(.c) void {
    v.g.vs_battle_doorEntered().* = psx.conv(i16, door);
    if (psx.get(v.g.vs_battle_roomData().sectionF)) |scene| {
        var p: [*]i32 = @ptrCast(scene);
        p = skip(p, scene.roomCount *% 3 +% 1);
        p = skip(p, p[0] *% 2 +% 1);
        p = skip(p, p[0] +% 1);
        p = skip(p, p[0] +% 1);
        p = skip(p, p[0] +% 1);
        p = skip(p, p[0] +% 1);
        const count = p[0];
        p = skip(p, 1);
        var i: c_int = 0;
        while (i < count) : (i +%= 1) {
            if (i == door) {
                @as([*]u8, @ptrCast(p))[2] |= 0x10;
            }
            p = skip(p, 1);
        }
    }
}
