//! Finds the room section at the player's cell and returns it if it is active and matches the actor's Y (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_findFacingUnlootedChest (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the room section at the cell of `arg0`'s position if it is not flagged off and its height matches, else null.
pub fn vs_field_findFacingUnlootedChest(arg0: c_int) callconv(.c) ?*v._mpdRoomSection13 {
    var sp10: v.func_8006EBF8_t = undefined;
    var sp20: [4]u8 = undefined;

    _ = c.vs_actors_actorPlacement(arg0, &sp10);
    const temp_s2: c_int = c.vs_field_tileInFrontOfActor(@ptrCast(&sp10), &sp20);
    const temp_v0: ?*v._mpdRoomSection13 = @ptrCast(@alignCast(c.vs_items_treasureChestAtTile(
        @as(c_int, sp20[0]),
        @as(c_int, sp20[2]),
        @as(c_int, sp20[1]),
    )));

    if (temp_v0) |room| {
        if (@as(c_int, room.unk12) == temp_s2) {
            if (c.vs_battle_getStateFlag(psx.conv(c_short, @as(c_int, room.unk10) +% 0x3C0)) == 0) {
                // Sign-extend the 16-bit height: shift up 17, arithmetic shift back down.
                const height: c_int = @as(c_int, c.vs_field_floorHeightAt(
                    ((@as(c_int, sp20[0]) << 7) | 0x40),
                    ((@as(c_int, sp20[2]) << 7) | 0x40),
                ));
                const expected: c_int = ((height << 17) >> 17) +% 0x60;
                if (@as(c_int, sp10.unk0.unk4.vy) == expected) {
                    return room;
                }
            }
        }
    }
    return null;
}
