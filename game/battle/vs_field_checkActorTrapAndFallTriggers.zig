//! Battle actor update from a room-section probe: writes the actor's record and
//! reports the outcome (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_checkActorTrapAndFallTriggers (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Updates actor arg0's record from the room-section probe arg1 and returns the outcome (0 nothing, 1 or 2 the death paths, 3 a section handled).
pub fn vs_field_checkActorTrapAndFallTriggers(arg0: c_int, arg1: ?*v.vs_actors_ActorLocation) callconv(.c) c_int {
    const temp_v0: c_uint = psx.conv(c_uint, c.vs_actors_actorStateFlags(arg0));
    const temp_a2: c_uint = temp_v0 & 0x70000;
    const temp_s3: c_int = psx.conv(c_int, (temp_v0 >> 14) & 1);

    const temp_s4: ?*v._mpdRoomSection3 = if (temp_a2 != 0)
        @ptrCast(c.vs_field_collisionCellAt(0, 0, psx.conv(c_int, temp_a2 >> 16)))
    else
        @ptrCast(c.vs_field_collisionCellAt(
            psx.conv(c_uint, psx.getBits(&arg1.?.packedTile.fields, "unk0_0")),
            psx.conv(c_uint, psx.getBits(&arg1.?.packedTile.fields, "unk0_16")),
            psx.conv(c_int, psx.getBits(&arg1.?.packedTile.fields, "unk0_8")),
        ));

    if (v.g.vs_battle_screenTransitionStep().* == 0) {
        if ((temp_v0 & 0x0100_0000) == 0) {
            const temp_s0: *v.vs_battle_actor = psx.at(v.g.vs_battle_actors(), arg0).*.?;

            if ((arg1.?.packedTile.value & 0xFFFFFF) != (temp_s0.unk2C.s32 & 0xFFFFFF) or
                temp_s3 != @as(c_int, temp_s0.unk2C.u8[3]) or
                (temp_v0 & 0x70000) != 0)
            {
                if (v.g.D_800F196C().* != 3) {
                    if (psx.getBits(temp_s4.?, "unk0_16") != 0) {
                        if ((temp_v0 & 0x1670_4000) == 0) {
                            const temp_v0_2: ?*v._mpdRoomSection9 = @ptrCast(c.vs_field_findRoomTriggerAtTile(
                                psx.conv(c_int, psx.getBits(&arg1.?.packedTile.fields, "unk0_0")),
                                psx.conv(c_int, psx.getBits(&arg1.?.packedTile.fields, "unk0_16")),
                                psx.conv(c_int, psx.getBits(&arg1.?.packedTile.fields, "unk0_8")),
                            ));
                            if (temp_v0_2) |sec| {
                                if (sec.unk6 != 0x10) {
                                    c.vs_field_beginTrapInteraction(@ptrCast(sec), @ptrCast(arg1), arg0);
                                    temp_s0.unk2C.s32 = arg1.?.packedTile.value;
                                    temp_s0.unk2C.u8[3] = psx.conv(u8, temp_s3);
                                    return 3;
                                }
                            }
                        }
                    }
                }
                if ((temp_v0 & 0x504000) == 0) {
                    temp_s0.unk2C.s32 = arg1.?.packedTile.value;
                }
                temp_s0.unk2C.u8[3] = psx.conv(u8, temp_s3);
            }

            if (psx.getBits(temp_s4.?, "unk0_17") != 0) {
                if (arg1.?.position.vy >= 0x11) {
                    psx.get(psx.at(v.g.vs_battle_actors(), arg0).*.?.stats).?.currentHP = 0;
                    _ = c.vs_battle_resolveActorDefeat(0, arg0, 0);
                    if (@as(c_int, c.vs_event_evaluateRoomEventTriggers(0xC, 0, 0, 0)) == 1) {
                        return 1;
                    }
                    return 2;
                }
            }
        }
    }
    return 0;
}
