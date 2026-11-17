//! Starts an actor's move one tile along the direction its object selects
//! (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: vs_field_beginGimmickInteraction (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Sets up the actor's object step and moves the actor one tile; returns -1 when it cannot.
pub fn vs_field_beginGimmickInteraction(actorId: c_int) callconv(.c) c_int {
    const actor: *v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), actorId).*.?;
    if ((psx.bitsAt(c_uint, &actor._bits_unk5AC_0, 0).* & 0x100000) != 0) {
        return -1;
    }
    const objectId = c.vs_field_findInteractableGimmickAhead(actorId);
    if (objectId == -1) {
        return -1;
    }
    const object: *v.D_800F45E0_t = psx.at(v.g.vs_field_gimmickInstances(), objectId).*.?;
    object.unk3A = psx.conv(i16, v.g.vs_field_gimmickInteractionDirection().*);
    v.g.vs_field_interactingGimmickId().* = objectId;
    var height: c_int = @as(c_int, actor.unk0.position.vy) -% @as(c_int, object.unk1E);
    height = @divTrunc(height, 64);
    height +%= 1;

    object.unk34 = 0;
    object.unk38 = 0;
    const mode = psx.fieldAt(u8, &object.unk6C, 256).* & 7;
    var step: c_int = 0;
    if (mode <= 3) {
        if (mode <= 1) {
            const idx: c_int = @as(c_int, object.unk6C[8].unk2) *% 4 +% v.g.vs_field_gimmickInteractionDirection().*;
            object.unk6C[8].unk2 = psx.at(v.g.D_800E8FD0(), idx).*;
        }
        if (mode == 3) {
            step = 0x20;
            object.unk34 = 0;
            object.unk38 = 0;
            if (v.g.vs_field_gimmickInteractionDirection().* < 2) step = -0x20;
        } else {
            step = 0x80;
            if (v.g.vs_field_gimmickInteractionDirection().* < 2) step = -0x80;
        }
        if ((v.g.vs_field_gimmickInteractionDirection().* & 1) != 0) {
            object.unk34 = psx.conv(i16, step);
        } else {
            object.unk38 = psx.conv(i16, step);
        }
        // shared tail of cases 2 and 3 (the C's `move:` label)
        object.unk36 = 0;
        _ = c.vs_audio_playActorSoundPair(@ptrCast(actor), 0x2C, 0x2D);
        _ = c.vs_actors_playActorAnimation(actorId, height +% 0x4F, 0, 4);
        psx.bitsAt(c_uint, &actor._bits_unk5AC_0, 0).* |= 0x100000;
        _ = c.vs_battle_aiReserveAiDestinationTile(
            @divTrunc(@as(c_int, object.unk1C) +% @as(c_int, object.unk34), 128),
            @divTrunc(@as(c_int, object.unk20) +% @as(c_int, object.unk38), 128),
        );
    } else if (mode >= 4 and mode <= 6) {
        if ((psx.bitsAt(c_uint, &actor._bits_unk5AC_0, 0).* & 8) != 0) {
            _ = c.vs_actors_playActorAnimation(actorId, 0x4C, 0, 4);
            _ = c.vs_audio_playActorSoundPair(@ptrCast(actor), 0x2E, 0x2F);
            psx.bitsAt(c_uint, &actor._bits_unk5AC_0, 0).* |= 0x100000;
        } else {
            _ = c.vs_actors_playActorAnimation(actorId, height +% 0x47, 0, 4);
            _ = c.vs_audio_playActorSoundPair(@ptrCast(actor), 0x2C, 0x2D);
            psx.bitsAt(c_uint, &actor._bits_unk5AC_0, 0).* |= 0x100000;
        }
        var dx: c_int = 0;
        var dz: c_int = 0;
        step = 1;
        if (v.g.vs_field_gimmickInteractionDirection().* < 2) step = -1;
        if ((v.g.vs_field_gimmickInteractionDirection().* & 1) != 0) {
            dx = step;
        } else {
            dz = step;
        }
        _ = c.vs_battle_aiReserveAiDestinationTile(
            @as(c_int, actor.unk0.currentTileX) +% dx,
            @as(c_int, actor.unk0.currentTileZ) +% dz,
        );
    }

    // The 4-byte tile copy: x, y, z, direction, as the four bytes of the actor's tile fields.
    step = 1;
    var tile: [4]u8 = .{
        actor.unk0.currentTileX,
        actor.unk0.unk5D,
        actor.unk0.currentTileZ,
        actor.unk0.unk5F,
    };
    if (v.g.vs_field_gimmickInteractionDirection().* < 2) step = -1;
    if ((v.g.vs_field_gimmickInteractionDirection().* & 1) != 0) {
        tile[0] = psx.conv(u8, @as(c_int, tile[0]) +% step);
    } else {
        tile[2] = psx.conv(u8, @as(c_int, tile[2]) +% step);
    }
    _ = c.vs_actors_turnActorTowardTileOverTime(actorId, @ptrCast(&tile), 4);

    // pad is uninitialised in the original; it is zeroed here.
    var position: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    position.vx = psx.conv(i16, (@as(c_int, actor.unk0.currentTileX) << 7) +% 0x40);
    position.vz = psx.conv(i16, (@as(c_int, actor.unk0.currentTileZ) << 7) +% 0x40);
    position.vy = actor.unk0.position.vy;
    _ = c.vs_actors_translateActorToOverTime(actorId, @ptrCast(&position), 4);
    // The C falls off the end after a void call: eax is the callee's leftover, which
    // the port does not model; 0 is returned.
    return 0;
}
