/* Host-only room submission candidate retained from decompilation work:
 * .work/ultracode/r8-146C/8ac78/v16.c (see docs/decomp-blockers.md).
 * Near-matching, not an original-binary match; minimap branches need runtime QA.
 * Included in 146C.c to use the same private types and globals. */
extern int D_800F1BA0;
void func_8008DF14(void);
void func_8009627C(void);
void func_8008C0C0(void);
void func_80091B04(void);
void func_8008CEB0(void);
void func_80090EEC(void);
void func_8008B1FC(void*);
void func_8008B590(MATRIX*, int);
void func_80098F8C(vs_battle_scene*);
/* Widescreen: the area map is anchored to the right edge of the screen
 * (gpu_hw.c); its rooms are projected without the GTE's Hor+ narrowing so
 * they scale with their frame and stay inside it at every rotation. */
extern int pc_gte_anchored;
extern void pc_hw_anchor_packets(const void* start, const void* end, int anchor)
    __attribute__((weak));
#define PC_ANCHOR_RIGHT 2
static void anchor_map(void* start)
{
    if (pc_hw_anchor_packets)
        pc_hw_anchor_packets(start, psx_get(((vs_scratch_t*)0x1F800000)->unk0), PC_ANCHOR_RIGHT);
}

void func_8008AC78(void)
{
    vs_battle_geomOffset savedOffset;
    vs_battle_geomOffset mapOffset;
    RECT clip;
    MATRIX rotation;
    DR_AREA* area;
    void* mapPackets;
    LINE_G4* frame;
    LINE_G2* edge;
    LINE_G2* line;
    u_short state;
    int i;
    int pulse;
    int red;
    int left;
    int right;
    int front;

    SetRotMatrix(&((vs_scratch_t*)0x1F800000)->viewMatrix);
    SetTransMatrix(&((vs_scratch_t*)0x1F800000)->viewMatrix);
    func_8008DF14();
    func_8009627C();
    func_8008C0C0();
    if (D_800F1BA4 == 0) {
        for (i = 0; i < vs_gametime_tickspeed >> 1; ++i) {
            func_80091B04();
        }
    }
    func_8008CEB0();
    func_80090EEC();

    state = *(u_short*)&vs_battle_roomData;
    if (state != 0x101) {
        return;
    }
    if (psx_get(vs_battle_roomData.geometrySection) != NULL) {
        left =
            1 << (((0x9C0 - ((vs_scratch_t*)0x1F800000)->camera.angles.vy) & 0xFFF) >> 9);
        right =
            1 << (((0x840 - ((vs_scratch_t*)0x1F800000)->camera.angles.vy) & 0xFFF) >> 9);
        front =
            1 << (((0x900 - ((vs_scratch_t*)0x1F800000)->camera.angles.vy) & 0xFFF) >> 9);
        ((vs_scratch_t*)0x1F800000)->firstPersonViewFacing =
            (((vs_scratch_t*)0x1F800000)->firstPersonViewFacing & ~0xFF) | left | right
            | front;
        func_8008B1FC(psx_get(vs_battle_roomData.geometrySection));
    }
    if ((*(u_short*)&vs_battle_roomData != state) || !(vs_main_settings.simpleMap & 1)
        || (psx_get(vs_battle_roomData.sectionF) == NULL)) {
        return;
    }

    if (D_800F1CD6) {
        if (++D_800F1CD4 > 8) {
            D_800F1CD4 = 8;
        }
    } else {
        D_800F1CD4 -= 2;
        if (D_800F1CD4 < 0) {
            D_800F1CD4 = 0;
        }
    }
    if (D_800F1CD4 <= 0) {
        return;
    }

    func_8008B590(&rotation, 0x800 - ((vs_scratch_t*)0x1F800000)->camera.angles.vy);
    SetRotMatrix(&rotation);
    area = psx_get(((vs_scratch_t*)0x1F800000)->unk0);
    mapPackets = area;
    SetDrawArea(area, &vs_main_drawEnv[(vs_main_frameBuf + 1) & 1].clip);
    AddPrim((u_long*)psx_get(((vs_scratch_t*)0x1F800000)->unk4) + 5, area++);
    psx_set(((vs_scratch_t*)0x1F800000)->unk0, area);

    if (vs_main_drawEnv[(vs_main_frameBuf + 1) & 1].clip.x < 320) {
        clip.x = 216;
    } else {
        clip.x = 536;
    }
    clip.y = 156;
    clip.w = 96;
    clip.h = 64;
    mapOffset.y = 196;
    mapOffset.x = 392 - (D_800F1CD4 << 4);
    vs_battle_getGeomOffset(&savedOffset);
    vs_battle_setGeomOffset(&mapOffset);
    pc_gte_anchored = 1;
    func_80098F8C(psx_get(vs_battle_roomData.sectionF));
    pc_gte_anchored = 0;
    vs_battle_setGeomOffset(&savedOffset);
    SetRotMatrix(&((vs_scratch_t*)0x1F800000)->viewMatrix);

    area = psx_get(((vs_scratch_t*)0x1F800000)->unk0);
    SetDrawArea(area, &clip);
    AddPrim((u_long*)psx_get(((vs_scratch_t*)0x1F800000)->unk4) + 5, area++);
    psx_set(((vs_scratch_t*)0x1F800000)->unk0, area);

    if (!(vs_main_settings.simpleMap & 2)) {
        anchor_map(mapPackets);
        return;
    }

    frame = (LINE_G4*)area;
    setLineG4(frame);
    setXY4(frame, 215, 155, 313, 155, 313, 220, 215, 220);
    setSemiTrans(frame, 1);
    AddPrim((u_long*)psx_get(((vs_scratch_t*)0x1F800000)->unk4) + 5, frame);
    line = (LINE_G2*)(frame + 1);
    setLineG2(line);
    setSemiTrans(line, 1);
    line->y0 = 220;
    line->x0 = 215;
    line->x1 = 215;
    line->y1 = 155;
    edge = line++;
    AddPrim((u_long*)psx_get(((vs_scratch_t*)0x1F800000)->unk4) + 5, edge);
    psx_set(((vs_scratch_t*)0x1F800000)->unk0, line);
    _insertTPage(5, 96);
    anchor_map(mapPackets);

    if (vs_battle_characterState->weaponDrawn == 1) {
        pulse = rsin(D_800F1BA0 << 8) / 64;
        D_800F1BA0 += vs_gametime_tickspeed / 2;
        red = 128 - pulse;
        setRGB0(frame, red, 0, 0);
        setRGB1(frame, red + 32, 0, 0);
        setRGB2(frame, red + 63, 0, 0);
        setRGB3(frame, red + 32, 0, 0);
        setRGB0(edge, red + 32, 0, 0);
        setRGB1(edge, red, 0, 0);
    } else {
        D_800F1BA0 = 0;
        setRGB0(frame, 0, 128, 0);
        setRGB1(frame, 0, 192, 0);
        setRGB2(frame, 0, 255, 0);
        setRGB3(frame, 0, 192, 0);
        setRGB0(edge, 0, 192, 0);
        setRGB1(edge, 0, 128, 0);
    }
}

/* Host-only query for the interactive fog (dev_state.c pc_dev_battle_action,
 * fog_sim.cpp): the battle action running now (D_800F196C 2), one at a time
 * for every actor: its performer, action, phase (_cameraMode: 5 an effect
 * action started casting, 7 a swing or a release started (func_80070278, once
 * per swing, chain step and release), 8 a chain step succeeded, 9 ending),
 * the chain step, its kind (vs_main_actions: type and unk2_0, 0 a plain
 * attack), its aim point (unk844, world units) and its first target, the
 * actor func_800780A8 frames with the performer (unk4C[0] when it has
 * targets and that target is an actor; -1: none, or a point). */
int pc_battle_action(int* actor, int* action, int* phase, int* chain, int* type, int* plain, int aim[3], int* target)
{
    D_800F19CC_t2* a;
    if (D_800F196C != 2 || D_800F19CC == NULL)
        return 0;
    a = &D_800F19CC->unk854[D_800F19CC->unk0 & 3];
    *actor = (u_char)a->unk4.unk0.targetActor;
    *action = a->actionIndex;
    *phase = (int)_cameraMode;
    *chain = (int)D_800F19CC->unk0;
    *type = a->actionIndex < 256 ? vs_main_actions[a->actionIndex].type : 0;
    *plain = a->actionIndex < 256 ? vs_main_actions[a->actionIndex].unk2_0 == 0 : 1;
    aim[0] = D_800F19CC->unk8.unk844.vx;
    aim[1] = D_800F19CC->unk8.unk844.vy;
    aim[2] = D_800F19CC->unk8.unk844.vz;
    *target = a->unk4A != 0 && a->unk4C[0].unk40 == 0 ? (int)(u_char)a->unk4C[0].unk0.targetActor : -1;
    return 1;
}

/* Host-only query for the depth of field's camera subject (post_profile.cpp,
 * dof_focus `subject`; dev_state.c pc_dev_battle_camera): what BATTLE's camera
 * steps toward now, read the way func_8006E640 reads it in the action state
 * (D_800F196C 2): in _cameraMode 1 and 3 the chosen target candidate
 * (unk29C0[unk298D], while one is chosen: the targeting sphere's), in mode 2
 * the area point (unk2998); and the camera's look-at (vs_scratch.camera,
 * world units). out: [0] D_800F196C (-1: no battle state), [1] _cameraMode,
 * [2] a target candidate chosen, [3..5] its point (the body part's, or the
 * actor's: func_800719FC's list), [6] an area point, [7..9] it, [10..12] the
 * look-at, [13] the candidate's actor slot (unk9). Reads only. */
void pc_battle_camera_focus(int out[14])
{
    int i;
    for (i = 0; i < 14; ++i)
        out[i] = 0;
    out[0] = D_800F19CC != NULL ? (int)D_800F196C : -1;
    out[1] = (int)_cameraMode;
    if (D_800F19CC != NULL && D_800F196C == 2) {
        if ((_cameraMode == 1 || _cameraMode == 3) && D_800F19CC->unk298D != 0xFF && D_800F19CC->unk2C00 != 0
            && D_800F19CC->unk298D < 23) {
            out[2] = 1;
            out[3] = D_800F19CC->unk29C0[D_800F19CC->unk298D].unk0.vx;
            out[4] = D_800F19CC->unk29C0[D_800F19CC->unk298D].unk0.vy;
            out[5] = D_800F19CC->unk29C0[D_800F19CC->unk298D].unk0.vz;
            out[13] = (u_char)D_800F19CC->unk29C0[D_800F19CC->unk298D].unk9;
        } else if (_cameraMode == 2) {
            out[6] = 1;
            out[7] = D_800F19CC->unk2998.vx;
            out[8] = D_800F19CC->unk2998.vy;
            out[9] = D_800F19CC->unk2998.vz;
        }
    }
    out[10] = vs_scratch.camera.lookAt.vx >> 12;
    out[11] = vs_scratch.camera.lookAt.vy >> 12;
    out[12] = vs_scratch.camera.lookAt.vz >> 12;
}

/* Host-only queries for gameplay_walls.c (smart cutaway walls). */
extern char D_800F4C34; /* the event runner's state (4A0A8.c): nonzero while an event loads or runs */
int pc_battle_event_running(void) { return D_800F4C34 != 0; }
/* The running event's id as func_800BEB10 gives it ((source << 12) | event file;
 * source 0 the zone's event file, 1 the room's entry script, 2 a door's), or
 * -1 when none runs (post_profile.cpp: `post event.<id>` items). */
extern u_short D_800F4BE0;
int pc_battle_event_id(void) { return D_800F4C34 != 0 ? (int)(u_short)((D_800F4BE0 << 12) | _evtFile) : -1; }
/* The event interpreter's place, for the cutscene player (cutscene.cpp): state[0] the runner's
 * state (D_800F4C34: 0 none, 1-3 loading, 4 running), state[1] the skip state (D_800F4C2C: 0 not
 * skippable, 1 a skippable part, 2 skipping); per script thread (0 the main one, 1-3 forked) its
 * offset in the script buffer (-1: not running) and the opcode there, and that opcode's handler. */
extern u_char* D_800F4C38[];
extern int (*_opcodeFunctionTable[256])(u_char*, short);
int pc_battle_event_threads(int state[2], int offset[4], int opcode[4], void* handler[4])
{
    int i;
    state[0] = D_800F4C34;
    state[1] = D_800F4C2C;
    for (i = 0; i < 4; ++i) {
        u_char* pc = D_800F4C34 != 0 ? D_800F4C38[i] : NULL;
        offset[i] = pc != NULL && D_800F4C30 != NULL ? (int)(pc - (u_char*)D_800F4C30) : -1;
        opcode[i] = pc != NULL ? *pc : -1;
        handler[i] = pc != NULL ? (void*)_opcodeFunctionTable[*pc] : NULL;
    }
    return D_800F4C34 != 0;
}
/* The battle camera's target (weapon drawn, a target chosen: the point the
 * camera follows in its modes 1 and 3), as func_8007ACB0's caller reads it. */
int pc_battle_camera_target(int position[3])
{
    if (vs_battle_characterState == NULL || vs_battle_characterState->weaponDrawn != 1 || D_800F19CC == NULL
        || D_800F19CC->unk298D == 0xFF || D_800F19CC->unk2C00 == 0 || D_800F19CC->unk298D >= 23)
        return 0;
    position[0] = D_800F19CC->unk29C0[D_800F19CC->unk298D].unk0.vx;
    position[1] = D_800F19CC->unk29C0[D_800F19CC->unk298D].unk0.vy;
    position[2] = D_800F19CC->unk29C0[D_800F19CC->unk298D].unk0.vz;
    return 1;
}
