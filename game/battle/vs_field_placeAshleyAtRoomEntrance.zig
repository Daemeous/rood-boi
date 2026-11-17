//! Camera 0 setup from the room door section: the view-matrix block and the
//! first-person facing (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_placeAshleyAtRoomEntrance (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets camera 0 from the room door section and refreshes its view matrix.
pub fn vs_field_placeAshleyAtRoomEntrance() callconv(.c) void {
    var sp10: v.SVECTOR = undefined;
    var sp18: v.func_8006EBF8_t = undefined;
    var sp28: v.vs_field_ActorPlacement = undefined;
    const door = v.g.vs_field_pendingDestinationDoor();

    c.func_8009D458();

    sp28.unk0_0 = psx.conv(u8, psx.getBits(door, "unk4_8"));
    sp28.unk0_16 = psx.conv(u8, psx.getBits(door, "unk6_10"));
    sp28.unk0_8 = psx.conv(u8, psx.getBits(&door.tileLocation, "unk0_13"));
    // unk4_13 is 0..3, so the product is 0..192 and fits the 8-bit field.
    psx.setBits(&sp28, "unk0_24", psx.conv(u8, @as(c_int, psx.getBits(door, "unk4_13")) *% 64));
    c.vs_actors_placeActorInRoom(0, @ptrCast(&sp28), -0x800, 2);

    if (psx.getBits(door, "unk4_15") != 0) {
        switch (@as(c_int, psx.getBits(door, "unk4_13"))) {
            0, 2 => {
                sp10.vx = 0x40;
                sp10.vz = 0;
                sp10.vy = 0;
            },
            1, 3 => {
                sp10.vx = 0;
                sp10.vz = 0x40;
                sp10.vy = 0;
            },
            // Unreachable: unk4_13 is 2 bits. vx and vz stay unset, as in the C.
            else => {
                sp10.vy = 0;
            },
        }
        c.vs_actors_translateActorByAtRate(0, psx.cptr(c.SVECTOR, &sp10), 0);
    }

    _ = c.vs_actors_actorPlacement(0, &sp18);
    c.func_8009E5C4(0);
    c.func_800A4828(0, psx.cptr(c.MATRIX, &v.g.vs_view().viewMatrix));
    c.vs_field_setActorCollisionBypass(0, 0);
    c.vs_battle_aiInitializeRoomNavigation();
}
