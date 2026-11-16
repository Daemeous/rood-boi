//! Battle room load: the MPD sections, scene state and the opening camera (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_finishRoomLoad (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Loads the battle room from its MPD and sets up the scene, the camera and the screen transition.
pub fn vs_field_finishRoomLoad(arg0: c_int) callconv(.c) void {
    var sp20: v.VECTOR = undefined;
    var lookAt: v.VECTOR = undefined;
    var sp40: v.func_8006EBF8_t = undefined;
    var tempS4: c_int = undefined;
    var a0: c_int = undefined;
    const zone = v.g._zoneContext();

    if (psx.get(zone.mpdCdFile) == null) {
        c._loadMpd(arg0);
    }

    while (@as(c_int, psx.get(zone.mpdCdFile).?.state) != c.vs_main_CdQueueStateLoaded) {
        _ = c.vs_main_gametimeUpdate(0);
    }

    c.vs_main_freeCdQueueSlot(psx.cptr(c.vs_main_CdQueueSlot, psx.get(zone.mpdCdFile).?));
    psx.put(&zone.mpdCdFile, null);

    v.g.vs_render_legacyPrimitiveBuffers()[0] = c.vs_main_allocHeap(0x20000);
    v.g.vs_render_legacyPrimitiveBuffers()[1] = c.vs_main_allocHeap(0x20000);

    c.vs_main_clutInit();

    const mpdHeader: *anyopaque = psx.get(zone.mpdData).?;
    const hdr: *align(1) v._mpdHeader = @ptrCast(mpdHeader);

    _ = c._loadMpdRoomSection(0, @ptrFromInt(@as(usize, psx.conv(u32, hdr.roomSectionOffset) +% psx.addr(mpdHeader))));

    zone.zndId = v.g.vs_field_pendingDestinationDoor().zndId;
    zone.mapId = v.g.vs_field_pendingDestinationDoor().mpdId;

    a0 = c.vs_battle_getCurrentSceneId();

    if (a0 != zone.currentSceneId) {
        zone.currentSceneId = a0;
        c.func_80088CAC();
    }

    if (v.g.vs_state_sceneTimer().resetScope == 0) {
        c.vs_state_resetSceneTimer();
    }

    v.g.vs_field_isRoomVisitPending().* = 1;
    a0 = c._getCollisionMapDimensions(0);
    const lo: c_int = a0 & 0xFFFF;
    zone.tileColumns = psx.conv(i16, @divTrunc(lo, 16));

    if (a0 < 0) {
        a0 +%= 0xFFFFF;
    }
    zone.tileRows = psx.conv(i16, a0 >> 20);

    if (c._getMapBounds(0) != null) {
        zone.bounds = @as(*const v._zoneContextBounds, @ptrCast(c._getMapBounds(0))).*;
    } else {
        zone.bounds.minZ = 0;
        zone.bounds.minX = 0;
        zone.bounds.maxX = zone.tileColumns;
        zone.bounds.maxZ = zone.tileRows;
    }

    {
        // The zone's unk48..unk54 words: the first and third cleared whole, the second's and fourth's low halves.
        zone.unk48 = 0;
        zone.unk4C = @bitCast(@as(u32, @bitCast(zone.unk4C)) & 0xFFFF0000);
        zone.unk50 = 0;
        zone.unk54 = @bitCast(@as(u32, @bitCast(zone.unk54)) & 0xFFFF0000);
    }

    c.vs_render_captureRoomBackgroundColors();

    if (hdr.lootSectionLen != 0) {
        _ = c._loadMpdLootSection(@ptrFromInt(@as(usize, psx.conv(u32, hdr.lootSectionOffset) +% psx.addr(mpdHeader))));
    }

    {
        const clearedAddr: u32 = psx.conv(u32, hdr.clearedSectionOffset) +% psx.addr(mpdHeader);
        const scriptAddr: u32 = psx.conv(u32, hdr.scriptSectionOffset) +% psx.addr(mpdHeader);
        const doorAddr: u32 = psx.conv(u32, hdr.doorSectionOffset) +% psx.addr(mpdHeader);
        const doorLen: u16 = hdr.doorSectionLen;
        tempS4 = c._loadMpdClearedScriptDoorSections(
            @ptrFromInt(@as(usize, clearedAddr)),
            hdr.clearedSectionLen,
            @ptrFromInt(@as(usize, scriptAddr)),
            hdr.scriptSectionLen,
            @ptrFromInt(@as(usize, doorAddr)),
            doorLen,
            @as(c_int, @intFromBool(v.g.vs_main_startState().* == 1)),
        );
    }

    if (v.g.vs_main_startState().* == 1) {
        v.g.vs_main_startState().* = 5;
    }

    _ = c._loadMpdEnemySection(
        @ptrFromInt(@as(usize, psx.conv(u32, hdr.enemySectionOffset) +% psx.addr(mpdHeader))),
        psx.conv(c_uint, hdr.enemySectionLen),
    );

    c.vs_main_freeHeapR(mpdHeader);

    psx.put(&zone.mpdData, null);

    c.vs_field_restoreRoomEnemyCache();
    c.vs_field_placeAshleyAtRoomEntrance();
    c.pc_room_warp_room_loaded();
    c.vs_field_spawnRoomActors();
    c.pc_room_warp_room_spawned();

    c.vs_field_snapRoomActorsToSupport();
    c.func_80093FEC(
        0,
        0,
        psx.get(v.g.vs_battle_characterState().*.?.stats).?.statuses & 0x21000,
        1,
    );
    c.vs_battle_removeActorStatuses(0x21000, @ptrCast(psx.get(v.g.vs_battle_characterState().*.?.stats)));

    v.g.D_800F19C8().* = 0;
    v.g._firstPersonViewEnabled().* = 0;
    v.g.D_800F1A50()[0] = 1;
    v.g.D_800F1868().* = 0;
    _ = c.vs_actors_setOtherActorLogicEnabled(1);
    v.g.D_8005E1D4().* = 0;
    v.g.D_8005E244().* = 0;
    v.g.D_8005E1C4().* = 0;
    v.g.vs_main_buttonRepeat().* = 0;
    v.g.vs_main_buttonsReleased().* = 0;
    v.g.vs_main_buttonsPressed().all = 0;
    v.g.vs_main_buttonsPreviousState().* = 0;
    v.g.vs_main_buttonsState().* = 0;
    v.g.vs_view().camera.farClip = 4096;

    if (v.g.vs_battle_screenTransitionEffect().* == 3) {
        v.g.vs_battle_screenTransitionStep().* = 0;
        v.g.vs_battle_screenTransitionAlpha().* = 0;
    } else {
        v.g.vs_battle_screenTransitionStep().* = 3;
    }

    c.vs_battle_setNearClip(4096 / 64);
    c.vs_battle_setProjectionDistance(4096 / 8);

    const cam = v.g.vs_battle_cameraCurrentSpherical();
    cam.delta.yaw = 0;
    cam.delta.distance = 0;
    cam.values.yaw = cam.delta.pitch;
    cam.values.distance = cam.initialDistance;
    if (cam.initialDistance == (3 * 4096 / 8)) {
        cam.delta.mode = 1;
    } else {
        cam.delta.mode = 3;
    }

    cam.values.pitch = -384;
    cam.unk24 = 0;

    if ((zone.zndId == 27) and (zone.mapId == 1)) {
        cam.values.pitch = -128;
    }

    v.g.D_800F1908().* = 0;

    _ = c.vs_actors_actorPlacement(0, &sp40);

    sp20.vx = @as(c_int, sp40.location.position.vx) *% 4096;
    sp20.vy = @as(c_int, sp40.location.position.vy) *% 4096 -% 90 * 4096;
    sp20.vz = @as(c_int, sp40.location.position.vz) *% 4096;

    c._clampPositionToZoneBounds(psx.cptr(c.VECTOR, &sp20), psx.cptr(c.VECTOR, &lookAt));

    v.g.vs_view().camera.lookAt = lookAt;
    v.g.vs_view().camera.angles.vy = cam.values.yaw;

    c._setCameraPositionFromAngles(psx.cptr(c._sphericalValues, &cam.values));
    _ = c.vs_actors_actorPlacement(0, &sp40);
    c.vs_field_updateCameraTileConstraint(0, @ptrCast(&sp40.location.packedTile.fields));
    c.func_8007D360();
    c.vs_main_commitClut();
    c.vs_render_configureFieldDisplay(0x140, 0xF0, v.g.vs_main_projectionDistance().*, 0, 0, 0);

    if ((tempS4 << 16) != 0) {
        _ = c.func_8008EA90(1);
        c.vs_event_prepareCutsceneActors();
    } else {
        if (c.vs_event_evaluateRoomEventTriggers(1, 0, 0, 0) == 1) {
            _ = c.vs_event_requestEventStart();
            _ = c.func_8008EA90(1);
            c.vs_event_prepareCutsceneActors();
        } else {
            c.func_8007BF2C();
            c.vs_actors_enableAllActorModels();
            _ = c.func_8008EA90(0);
            c.vs_field_restoreRoomEffectSoundVolume();
            c.func_8006F5CC();
            c.vs_battle_setHudHidden(0);
            if (v.g._zoneContext().musicSlot != 0) {
                _ = c.vs_audio_playMusicSlot(v.g._zoneContext().musicSlot, 0x7F, 0);
            }
            v.g.vs_battle_screenTransitionStep().* = 4;
            c.func_80089CE4();
        }
    }
    c.vs_main_padDisconnectAll();
}
