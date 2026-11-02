//! The door id of a door of the room (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// `p + k` as the C computes it: a signed int offset in ints, unchecked.
fn skip(p: [*]i32, k: c_int) [*]i32 {
    return @ptrCast(psx.at(p, k));
}

/// orig: _getDoorId (src/BATTLE/BATTLE.PRG/146C.c)
/// The id byte of the room's door `door` (its door list follows five counted lists in section F), or -1.
pub fn getDoorId(door: c_int) callconv(.c) c_int {
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
        if (door < count and door >= 0) {
            p = skip(p, door);
            return @as([*]u8, @ptrCast(p))[1];
        }
    }
    return -1;
}
