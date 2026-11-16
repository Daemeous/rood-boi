//! Places a battle actor at its placement: start position, facing and anchor (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: vs_actors_placeActorInRoom (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Places actor `actorId` at `placement`, on the floor, under the ceiling or on a platform, per `mode`.
pub fn vs_actors_placeActorInRoom(actorId: c_int, placement: ?*v.func_80089888_t, height: c_int, mode: c_int) callconv(.c) void {
    const actor: *v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), actorId).*.?;
    const pl = placement.?;

    psx.fieldAt(c_int, &actor.unk0.currentTileX, 0).* = psx.byteAt(c_int, pl, 0).*;
    actor.unk0.position.vx = psx.conv(i16, (@as(c_int, pl.unk0_0) << 7) + 0x40);
    const z: i16 = psx.conv(i16, (@as(c_int, pl.unk0_16) << 7) + 0x40);
    actor.unk0.position.vz = z;
    actor.unk0.position.vy = psx.conv(i16, c.vs_field_actorSupportHeight(actor.unk0.position.vx, z));

    var value: c_int = undefined;
    if (mode == 0x10 or mode == 0x20) {
        const ceiling = c.vs_battle_aiRoomNavigationCeilingHeight();
        if (ceiling == 0) {
            value = -0x180;
        } else {
            value = ceiling -% @as(c_int, actor.unk0.position.vy);
            if (value >= -0x7F) {
                value = -0x80;
            }
        }
        const position: *v.SVECTOR = &actor.unk0.position;
        actor.unk0.position.vy = psx.conv(i16, @as(c_int, psx.conv(u16, actor.unk0.position.vy)) +% value);
        if (c.vs_actors_findActorAtFlightPosition(actorId, psx.cptr(c.SVECTOR, position)) != 0) {
            _ = c.vs_actors_separateActorFlightPlacement(actorId, psx.cptr(c.SVECTOR, position));
        }
        psx.fieldAt(c_int, &actor.unk0._bits_skip, 0).* |= 0x200000;
        _ = c.vs_actors_playActorAnimation(actorId, 8, 0, 0);
    } else {
        if (mode == 0x40 or mode == 0x80) {
            psx.fieldAt(c_int, &actor.unk0._bits_skip, 0).* |= 0x400000;
        }
        const tileType: u8 = pl.unk0_8;
        if (tileType == 7) {
            actor.unk0.unk5D = 0;
            actor.unk0.position.vy = psx.conv(i16, height);
            actor.unk0.unk34.vy = 0;
            psx.fieldAt(c_uint, &actor.unk0._bits_skip, 0).* = (psx.fieldAt(c_uint, &actor.unk0._bits_skip, 0).* & 0xFFF8_FFFF) | 0x3_0000;
        } else {
            if (tileType >= 2) {
                const anchorPtr = c.func_800A4A24(tileType);
                var anchorAddr: usize = @intFromPtr(anchorPtr);
                // No matching platform: the original reads through NULL, i.e. PS1 address 0, mapped at 0x80000000.
                if (anchorAddr == 0) anchorAddr = 0x8000_0000;
                const anchor: [*]align(1) const c_int = @ptrFromInt(anchorAddr);
                psx.fieldAt(c_int, &actor.unk0.position.vx, 0).* = anchor[0];
                psx.fieldAt(c_int, &actor.unk0.position.vz, 0).* = anchor[1];
                psx.fieldAt(c_int, &actor.unk17EC, 0).* = anchor[0];
                psx.fieldAt(c_int, &actor.unk17EC, 4).* = anchor[1];
                psx.fieldAt(i16, &actor.unk17EC, 6).* = psx.conv(i16, tileType);
                const flags: c_int = (psx.fieldAt(c_int, &actor.unk0._bits_skip, 4).* & ~@as(c_int, 0xF)) | (@as(c_int, tileType) & 0xF);
                psx.fieldAt(c_int, &actor.unk0._bits_skip, 4).* = flags;
                psx.fieldAt(c_int, &actor.unk0._bits_skip, 4).* = (flags & ~@as(c_int, 0xF0)) | ((@as(c_int, tileType) & 0xF) * 0x10);
            }
            _ = c.vs_actors_playActorAnimation(actorId, 1, 0, 0);
        }
    }

    c.vs_actors_saveActorGroundedTransform(@ptrCast(actor));
    actor.unk0.unk1A = 0;
    value = psx.byteAt(u8, pl, 3).*;
    actor.unk0.facingDelta = 0;
    actor.unk0.unk18 = 0;
    actor.unk18D2 = 0;
    actor.unk18D4 = 0;
    actor.unk18D6 = 0;
    actor.unk18D8 = 0;
    value *%= 0x10;
    actor.unk0.facing = psx.conv(i16, value);
    _ = c.vs_actors_resetActorRunMomentum(actorId);
    actor.unk6E4 = 0;
}
