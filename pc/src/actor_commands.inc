/* Host-only retained candidate: battle-146C/75554/a6.full.c.
 * See docs/decomp-blockers.md. Original PS1 assembly remains unchanged. */

typedef struct {
    u_int state : 8;
    u_int command : 8;
    u_int target : 8;
    u_int effect : 2;
    u_int camera : 2;
    u_int unk28 : 4;
} actorCommandHeader;

int func_80075554(void)
{
    SVECTOR vector;
    func_8006EBF8_t position;
    func_8006EBF8_t target;
    vs_battle_actor* actor;
    u_int flags;
    vs_battle_actor2* stats;
    int i;
    int state;
    int angle;
    int speed;
    int result;

    for (i = 0; i < 16; ++i) {
        actor = vs_battle_actors[i];
        if (actor == NULL) {
            continue;
        }
        if (func_800A0BE0(i) & 0x1000000) {
            continue;
        }
        if ((actor->unk1C != 4) && (actor->unk1C != 2) && (D_800F196C != 3)) {
            continue;
        }

        if (actor->unk8 == 0x40) {
            state = actor->unk8;
            switch (actor->unk9) {
            case 9:
            case 10:
            case 11:
            case 12:
            case 14:
            case 15:
            case 18:
                if (!(func_800A0BE0(i) & 0x4000)) {
                    state = 0;
                }
                break;
            case 13:
                func_800753F8(actor, &vector);
                func_800B6500(i, &vector);
                state = ((actor->unkC | actor->unk10.s32 | (func_800A0BE0(i) & 0x4000)) != 0) << 6;
                break;
            case 23:
                if (!(func_800A0BE0(i) & 0x12000000)) {
                    state = 0;
                }
                break;
            case 20:
            case 21:
            case 22:
                if (!(func_800A0BE0(i) & 0x4000000)) {
                    state = 0;
                }
                break;
            default:
                vs_main_nop9(0xC1, 0);
                break;
            }
            actor->unk8 = state;
            if (D_800F196C == 3) {
                func_800A4E68(i);
            }
            continue;
        }

        if (actor->unk8 != 0x80) {
            continue;
        }

        func_800A1108(i, &position);
        flags = *(u_int*)&actor->unk8;
        if (flags & 0x3000000) {
            func_800AE4FC(psx_get(actor->unk44), (flags >> 24) & 3);
        }
        switch (((actorCommandHeader*)&actor->unk8)->camera) {
        case 1:
            func_8006D028(i);
            break;
        case 2:
            func_8006D064();
            break;
        }
        ((actorCommandHeader*)&actor->unk8)->camera = 0;

        switch (actor->unk9) {
        case 2:
            if (actor->unkA >= 16) {
                func_8009E614(i);
            } else {
                func_8009E5C4(i);
            }
            state = 0;
            break;

        case 3:
            func_8009FC60(i, -4, NULL, 0);
            func_800B6634(i, *(short*)&actor->unkC);
            state = 0;
            break;

        case 4:
            vector.vx = 0;
            vector.vy = *(short*)&actor->unkC;
            func_8009FC60(i, -3, &vector, -1);
            state = 0;
            func_800B668C(i, *(short*)&actor->unkC, ((short*)&actor->unkC)[1]);
            break;

        case 15:
            func_8009FC60(i, -2, NULL, -1);
            if (actor->unkA >= 16) {
                func_800B64CC(i, *(short*)&actor->unkC, ((short*)&actor->unkC)[1] << 12,
                    *(short*)&actor->unkC);
            } else {
                func_800A1108(actor->unkA, &target);
                func_800B64CC(i, *(short*)&actor->unkC, ((short*)&actor->unkC)[1] << 12,
                    ratan2(target.unk0.unk4.vx - position.unk0.unk4.vx,
                        target.unk0.unk4.vz - position.unk0.unk4.vz)
                        + 0x800);
            }
            state = ((func_800A0BE0(i) & 0x4000) != 0) << 6;
            break;

        case 9:
            func_8009FC60(i, -2, NULL, -1);
            stats = psx_get(actor->unk3C);
            if (stats->unk32 == 2) {
                speed = stats->unk33 << 12;
            } else if (stats->unk32 == 0x40) {
                speed = stats->unk33 << 12;
            } else {
                speed = stats->unk31 << 12;
            }
            if (actor->unkA < 16) {
                func_800A1108(actor->unkA, &target);
                func_800B64CC(i, *(short*)&actor->unkC, speed,
                    ratan2(target.unk0.unk4.vx - position.unk0.unk4.vx,
                        target.unk0.unk4.vz - position.unk0.unk4.vz)
                        + 0x800);
            } else {
                func_800B64CC(i, *(short*)&actor->unkC, speed, *(short*)&actor->unkC);
            }
            state = ((func_800A0BE0(i) & 0x4000) != 0) << 6;
            break;

        case 10:
            func_8009FC60(i, -2, NULL, -1);
            stats = psx_get(actor->unk3C);
            if (stats->unk32 == 4) {
                speed = stats->unk33 << 12;
            } else if (stats->unk32 == 0x80) {
                speed = stats->unk33 << 12;
            } else {
                speed = stats->unk31 << 12;
            }
            func_800B64CC(i, *(short*)&actor->unkC, speed, *(short*)&actor->unkC);
            state = ((func_800A0BE0(i) & 0x4000) != 0) << 6;
            break;

        case 11:
        case 12:
        case 14:
        case 18:
            func_8009FC60(i, -2, NULL, -1);
            target.unk0.unk4.vx = (((u_char*)&actor->unkC)[0] << 7) + 0x40;
            target.unk0.unk4.vz = (((u_char*)&actor->unkC)[2] << 7) + 0x40;
            angle = ratan2(target.unk0.unk4.vx - position.unk0.unk4.vx,
                        target.unk0.unk4.vz - position.unk0.unk4.vz)
                  + 0x800;
            switch (actor->unk9) {
            case 11:
            case 12:
                func_800B63AC(i, &actor->unkC, actor->unk10.s32, angle);
                break;
            case 18:
                func_800B6438(i, &actor->unkC, actor->unk10.s32, angle);
                break;
            case 14:
                func_800B6404(i, &actor->unkC, actor->unk10.s32, angle);
                break;
            }
            state = 0x40;
            break;

        case 13:
            func_800753F8(actor, &vector);
            func_800B6500(i, &vector);
            state = ((actor->unkC | actor->unk10.s32 | (func_800A0BE0(i) & 0x4000)) != 0) << 6;
            break;

        case 23:
            func_8009FC60(i, -2, NULL, -1);
            if (actor->unkA >= 16) {
                angle = position.unk0.unk4.pad;
            } else {
                func_800A1108(actor->unkA, &target);
                angle = ratan2((target.unk0.unk0.fields.unk0_0 - ((u_char*)&actor->unkC)[0]) << 12,
                            (target.unk0.unk0.fields.unk0_16 - ((u_char*)&actor->unkC)[2]) << 12)
                      + 0x800;
            }
            func_800B6580(i, &actor->unkC, angle);
            state = 0x40;
            break;

        case 16:
            func_8009FC60(i, -2, NULL, -1);
            if (psx_get(actor->unk3C)->unk32 == 0x10) {
                speed = psx_get(actor->unk3C)->unk33 << 12;
            } else {
                speed = psx_get(actor->unk3C)->unk31 << 12;
            }
            VectorNormalSS((SVECTOR*)&actor->unkC, &vector);
            if (actor->unkA >= 16) {
                angle = ratan2(vector.vx, vector.vz) + 0x800;
            } else {
                func_800A1108(actor->unkA, &target);
                angle = ratan2(target.unk0.unk4.vx - position.unk0.unk4.vx,
                            target.unk0.unk4.vz - position.unk0.unk4.vz)
                      + 0x800;
            }
            func_800B653C(i, &vector, speed, angle);
            state = 0;
            break;

        case 17:
            func_8009FC60(i, -2, NULL, -1);
            if (psx_get(actor->unk3C)->unk32 == 0x20) {
                speed = psx_get(actor->unk3C)->unk33 << 12;
            } else {
                speed = psx_get(actor->unk3C)->unk31 << 12;
            }
            VectorNormalSS((SVECTOR*)&actor->unkC, &vector);
            if (actor->unkA >= 16) {
                angle = ratan2(vector.vx, vector.vz) + 0x800;
            } else {
                func_800A1108(actor->unkA, &target);
                angle = ratan2(target.unk0.unk4.vx - position.unk0.unk4.vx,
                            target.unk0.unk4.vz - position.unk0.unk4.vz)
                      + 0x800;
            }
            func_800B653C(i, &vector, speed, angle);
            state = 0;
            break;

        case 19:
            vector.vx = actor->unkC / ONE - position.unk0.unk4.vx;
            vector.vy = actor->unk14 / ONE - position.unk0.unk4.vy;
            vector.vz = actor->unk10.s32 / ONE - position.unk0.unk4.vz;
            if (actor->unkA >= 16) {
                angle = ratan2(vector.vx, vector.vz) + 0x800;
            } else {
                func_800A1108(actor->unkA, &target);
                angle = ratan2(target.unk0.unk4.vx - position.unk0.unk4.vx,
                            target.unk0.unk4.vz - position.unk0.unk4.vz)
                      + 0x800;
            }
            state = 0;
            func_800B6530(i, &vector, angle);
            break;

        case 6:
            func_800B64CC(i, position.unk0.unk4.pad, 0, position.unk0.unk4.pad);
            state = 0;
            vector.vy = *(short*)&actor->unkC;
            vector.vx = 0;
            func_8009FC60(i, -4, &vector, ((short*)&actor->unkC)[1]);
            break;

        case 7:
            func_800B64CC(i, position.unk0.unk4.pad, 0, position.unk0.unk4.pad);
            state = 0;
            vector.vy = *(short*)&actor->unkC;
            vector.vx = 0;
            func_8009FC60(i, -4, &vector, 0x1000);
            break;

        case 22:
            if (actor->unkA >= 16) {
                vector.vx = ((short*)&actor->unkC)[0] / ONE - position.unk0.unk4.vx;
                vector.vy = ((short*)&actor->unkC)[1] / ONE - position.unk0.unk4.vy;
                vector.vz = *(short*)&actor->unk10 / ONE - position.unk0.unk4.vz;
                angle = ratan2(vector.vx, vector.vz) + 0x800;
            } else {
                func_800A1108(actor->unkA, &target);
                angle = ratan2(target.unk0.unk4.vx - position.unk0.unk4.vx,
                            target.unk0.unk4.vz - position.unk0.unk4.vz)
                      + 0x800;
            }
            state = 0x40;
            func_800B65F4(i, &actor->unkC, angle);
            break;

        case 21:
            if (actor->unkA >= 16) {
                vector.vx = ((u_char*)&actor->unkC)[0] / ONE - position.unk0.unk0.fields.unk0_0;
                vector.vy = ((u_char*)&actor->unkC)[1] / ONE - position.unk0.unk0.fields.unk0_8;
                vector.vz = ((u_char*)&actor->unkC)[2] / ONE - position.unk0.unk0.fields.unk0_16;
                angle = ratan2(vector.vx, vector.vz) + 0x800;
            } else {
                func_800A1108(actor->unkA, &target);
                angle = ratan2(target.unk0.unk4.vx - position.unk0.unk4.vx,
                            target.unk0.unk4.vz - position.unk0.unk4.vz)
                      + 0x800;
            }
            state = 0x40;
            func_800B65E8(i, &actor->unkC, angle);
            break;

        case 20:
            func_800A2574(i, position.unk0.unk4.pad);
            state = 0x40;
            break;

        case 8:
            if (!(func_800A0BE0(i) & 0x200000)) {
                vector.vx = 0;
                vector.vy = 0;
                vector.vz = 0;
                func_800B6500(i, &vector);
            }
            state = 0;
            break;

        case 24:
            func_8009FC60(i, -2, NULL, -1);
            state = (u_short)psx_get(actor->unk3C)
                        ->armor[((u_short*)&actor->unkC)[0]][((u_short*)&actor->unkC)[1]]
                        .id;
            if (state == 0) {
                state = 1;
            }
            func_80072EC4(actor->id, state);
            state = (D_800F19CC->unk298C == 0xFF) << 3;
            break;

        case 1:
        default:
            state = 0;
            break;
        }

        actor->unk8 = state;
        if (D_800F196C == 3) {
            func_800A4E68(i);
        } else if ((result = func_800751B8(i, &position.unk0)) != 0) {
            return result;
        }
    }
    return 0;
}
