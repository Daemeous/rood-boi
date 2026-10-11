/* Host-only object shadow submission, adapted from retained near-matching
 * .work/ultracode/r11-battle-b/ce15.c. See docs/decomp-blockers.md.
 * Original PS1 build keeps its assembly; runtime scene coverage is incomplete. */
void func_8008CEB0(void)
{
    SVECTOR vertices[4];
    psx_long interpolation;
    psx_long flags;
    _mpdRoomSection12* object;
    int count;
    int i;
    int height;
    int x;
    int altitude;
    int shade;
    int depth;
    POLY_F4* prim;
    _mpdRoomSection3* cell;

    SetRotMatrix(&((vs_scratch_t*)0x1F800000)->viewMatrix);
    SetTransMatrix(&((vs_scratch_t*)0x1F800000)->viewMatrix);

    if (psx_get(vs_battle_roomData.section12) == NULL) {
        return;
    }
    object = psx_get(vs_battle_roomData.section12);

    count = vs_battle_roomData.header.section12Len / sizeof(_mpdRoomSection12);
    for (i = 0; i < count; ++i, ++object) {
        if (object->unk0_9) {
            continue;
        }
        if (func_8009195C(object->unk0_0) == NULL) {
            continue;
        }
        cell = func_8008B764(object->unkA8 / 0x80000, object->unkB0 / 0x80000, 0);
        if (cell->unk0_17 && ((u_int)(cell->unk0_0 - 15) >= 4)) {
            continue;
        }
        x = object->unkA8 / ONE;
        height = func_8008DC7C(x, object->unkB0 / ONE);
        height <<= 17;
        height >>= 17;
        x = object->unkAC;
        altitude = -(x / ONE);
        if (altitude >= height) {
            continue;
        }
        altitude = -(x / ONE);
        shade = (height - altitude) / 2;
        if (shade >= 224) {
            shade = 223;
        }
        shade = 255 - shade;
        prim = psx_get(((vs_scratch_t*)0x1F800000)->unk0);
        vertices[0].vx = object->unkA8 / ONE - (32 + shade / 8);
        vertices[0].vy = height;
        vertices[0].vz = object->unkB0 / ONE - (32 + shade / 8);
        vertices[1].vx = object->unkA8 / ONE + (32 + shade / 8);
        vertices[1].vy = height;
        vertices[1].vz = object->unkB0 / ONE - (32 + shade / 8);
        vertices[2].vx = object->unkA8 / ONE - (32 + shade / 8);
        vertices[2].vy = height;
        vertices[2].vz = object->unkB0 / ONE + (32 + shade / 8);
        vertices[3].vx = object->unkA8 / ONE + (32 + shade / 8);
        vertices[3].vy = height;
        vertices[3].vz = object->unkB0 / ONE + (32 + shade / 8);
        depth = RotAverage4(&vertices[0], &vertices[1], &vertices[2], &vertices[3],
            (psx_long*)&prim->x0, (psx_long*)&prim->x1, (psx_long*)&prim->x2, (psx_long*)&prim->x3,
            &interpolation, &flags);
        if ((depth > 4) && (depth < 2048)) {
            setPolyF4(prim);
            setSemiTrans(prim, 1);
            setRGB0(prim, shade / 2 + 16, shade / 2 + 16, shade / 2 + 16);
            AddPrim((u_long*)psx_get(((vs_scratch_t*)0x1F800000)->unk4) + depth, prim++);
            psx_set(((vs_scratch_t*)0x1F800000)->unk0, prim);
            _insertTPage(depth, 64);
        }
    }
}
