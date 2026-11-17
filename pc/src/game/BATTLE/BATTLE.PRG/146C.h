#pragma once
#include "../../SLUS_010.40/main.h"
#include "2842C.h"
#include "3A1A0.h"
#include "scratch.h"
#include "pc/src/psx_types.h"

/* The ZUD file (an enemy's package, loaded by the zone's enemy table and by the ENEMY menu): a 0x30-byte header, then the parts
 * it names by byte offset from the file's start. The header is read in one place, vs_zud_decode (146C.c; the layout below is the
 * whole format); the parts stay raw in the file's buffer, where the object loader (vs_battle_populateDataSlot) reads them, and the
 * decoded form holds a pointer to each.
 *
 *   +0x00 u8 characterId, weapon, weaponCategory, weaponMaterial, shield, shieldMaterial, defaultMaterial, (pad)
 *   +0x08 u32 offset, i32 size of each part, in this order: shp (the model), weaponWep, shieldWep, commonSeq, battleSeq
 */
typedef struct {
    void* data;
    int size;
} vs_zud_part;

typedef struct {
    u_char characterId;
    u_char weapon;
    u_char weaponCategory;
    u_char weaponMaterial;
    u_char shield;
    u_char shieldMaterial;
    u_char defaultMaterial;
    vs_zud_part shp;
    vs_zud_part weaponWep;
    vs_zud_part shieldWep;
    vs_zud_part commonSeq;
    vs_zud_part battleSeq;
} vs_zud_file;

/* Reads the header of the ZUD file at `file` into `out`: each part's pointer is the file's address plus its offset. */
void vs_zud_decode(vs_zud_file* out, const void* file);

typedef struct {
    short width;
    short height;
    short unk4;
    short cellCount;
} _mpdRoomCollisionMapSection;

typedef struct {
    int unk0;
    int unk4;
    int unk8;
    int unkC;
    int unk10;
    int unk14;
    int unk18;
    int dataOffset;
    int unk20;
    int unk24;
    int unk28;
    int unk2C;
    int unk30;
    int unk34;
    int unk38;
    int unk3C;
} _mpdGeometryHeader;

typedef struct {
    short p1x, p1y, p1z;
    char p2x, p2y, p2z;
    char p3x, p3y, p3z;
    char r1, g1, b1, type;
    char r2, g2, b2, u3;
    char r3, g3, b3, v3;
    char u1, v1;
    short texelMask;
    char u2, v2;
    short idTexture;
} _mpdPolygt3;

typedef struct {
    short p1x, p1y, p1z;
    char p2x, p2y, p2z;
    char p3x, p3y, p3z;
    char r1, g1, b1, type;
    char r2, g2, b2, u3;
    char r3, g3, b3, v3;
    char u1, v1;
    short texelMask;
    char u2, v2;
    short idTexture;
    char p4x, p4y, p4z, u4;
    char r4, g4, b4, v4;
} _mpdPolygt4;

typedef struct {
    int numGroups;
    _mpdGeometryHeader groups[0];
} _mpdGeometrySection;

typedef struct {
    u_int unk0_0 : 5;
    u_int unk0_5 : 3;
    u_int unk0_8 : 1;
    u_int unk0_9 : 1;
    u_int unk0_10 : 3;
    u_int unk0_13 : 1;
    u_int unk0_14 : 1;
    u_int unk0_15 : 1;
    u_int unk0_16 : 1;
    u_int unk0_17 : 1;
    u_int unk0_18 : 2;
    u_int unk0_19 : 12;
} _mpdRoomSection3;

typedef struct {
    short tileX;
    short tileZ;
    short floorLayer;
    short unk6;
    union {
        short s16;
        struct {
            char state;
            char unk1;
        } lock;
    } unk8;
    char unkA;
    char unkB;
} _mpdRoomSection9;

typedef struct {
    _mpdRoomSection9 trigger;
    short keyOrSigil;
    short unkE;
    short linkedLockIndex;
    char unk12;
    char delayFrames;
} _mpdRoomSectionA;

typedef struct {
    int unk0[5];
    int unk14;
    int unk18;
    int unk1C;
    int unk20;
    int unk24;
    int unk28;
    int unk2C;
    char isWorkshop;
    char unk31;
    char trapLevel;
    char unk33;
    short unk34;
    short unk36;
    short unk38;
    short unk3A;
    short unk3C;
    short unk3E;
    short unk40;
    short unk42;
    short unk44;
    short unk46;
    union {
        short s16[2];
        int s32;
    } unk48[2];
    short sceneId;
    short unk52;
    short unk54;
    short unk56;
} _mpdRoomSectionB;

typedef struct {
    int unk0;
    int unk4;
    int unk8;
    u_int unkC; /* from the section's start: the entries after the header */
    u_int unk10; /* the same, past the first unk4 / 4 words; 0 when the section has no more */
    int unk14[0];
} _mpdRoomSectionE;
PSX_ASSERT_SIZE(_mpdRoomSectionE, 0x14);

typedef struct {
    int visited;
    u_int dataOffset; /* from the start of the scene: the room's data */
    short zoneId;
    short mapId;
} vs_battle_room;
PSX_ASSERT_SIZE(vs_battle_room, 0xc);

typedef struct {
    int roomCount;
    vs_battle_room rooms[0];
} vs_battle_scene;

typedef struct {
    u_short unk0_0 : 5;
    u_short unk0_5 : 5;
    u_short unk0_10 : 1;
    u_short unk0_11 : 3;
    u_short unk0_14 : 1;
    u_short unk0_15 : 1;
    u_short effectCount;
    char effectIds[0];
} _mpdRoomSection11;

typedef struct {
    u_int unk0_0 : 8;
    u_int unk0_8 : 1;
    u_int unk0_9 : 1;
    u_int unk0_10 : 2;
    u_int unk0_12 : 2;
    u_int unk0_14 : 2;
    u_int unk0_16 : 7;
    u_int unk0_23 : 3;
    u_int unk0_26 : 1;
    u_int unk0_27 : 2;
    u_int unk0_29 : 3;
    u_int unk4_0 : 1;
    u_int unk4_1 : 7;
    u_int unk4_8 : 2;
    u_int unk4_10 : 6;
    u_int unk4_16 : 16;
    VECTOR unk8[10];
    int unkA8;
    int unkAC;
    int unkB0;
    _mpdRoomSection3 unkB4;
} _mpdRoomSection12;

typedef struct {
    short tileX;
    short tileZ;
    short unk4;
    short unk6;
    short isOpen;
    short unkA;
    short unkC;
    short unkE;
    short openedFlag;
    short facing;
    vs_battle_loot loot;
} _mpdRoomSection13;

typedef struct {
    short minX;
    short minZ;
    short maxX;
    short maxZ;
} _zoneContextBounds;

typedef struct {
    char unk0;
    char unk1;
    short unk2;
    struct {
        u_int geometrySectionLen;
        u_int collisionMapSectionLen;
        u_int section3Len;
        u_int doorSectionLen;
        u_int lightingSectionLen;
        u_int section6Len;
        u_int section7Len;
        u_int section8Len;
        u_int section9Len;
        u_int sectionALen;
        u_int sectionBLen;
        u_int textureEffectsSectionLen;
        u_int sectionDLen;
        u_int sectionELen;
        u_int sectionFLen;
        u_int section10Len;
        u_int section11Len;
        u_int section12Len;
        u_int section13Len;
        u_int akaoSectionLen;
        u_int section15Len;
        u_int section16Len;
        u_int section17Len;
        u_int section18Len;
    } header;
    int* geometrySection;
    _mpdRoomCollisionMapSection* collisionMapSection;
    _mpdRoomSection3* section3;
    _mpdRoomDoorSection_t* doorSection;
    void* lightingSection;
    void* section6;
    void* section7;
    void* section8;
    _mpdRoomSection9* section9;
    _mpdRoomSectionA* sectionA;
    _mpdRoomSectionB* sectionB;
    void* textureEffectsSection;
    void* sectionD;
    _mpdRoomSectionE* sectionE;
    vs_battle_scene* sectionF;
    u_short* section10;
    _mpdRoomSection11* section11;
    _mpdRoomSection12* section12;
    _mpdRoomSection13* section13;
    u_int* section14;
    void* section15;
    void* section16;
    void* section17;
    _zoneContextBounds* section18;
} _mpdRoomSection;

typedef struct {
    /* 0x00 */ short class[2][8];
    /* 0x20 */ short affinity[2][8];
    /* 0x40 */
} vs_battle_classAffinityCurrent;

typedef struct {
    /* 0x00 */ short class[8];
    /* 0x10 */ u_short affinity[8];
    /* 0x20 */
} vs_battle_classAffinityBaseline;

typedef struct {
    u_short unk0[0x18];
} vs_battle_uiWeapon_unk;

typedef struct {
    u_short unk0;
    u_short unk2;
    char unk4;
    char unk5;
    char unk6;
    char unk7;
} func_80103080_t;

typedef struct {
    /* 0x00 */ u_short id;
    /* 0x02 */ signed char subId;
    /* 0x03 */ signed char wepId;
    /* 0x04 */ char category;
    /* 0x05 */ signed char strength;
    /* 0x06 */ signed char intelligence;
    /* 0x07 */ signed char agility;
    /* 0x08 */ u_short currentDp;
    /* 0x0A */ u_short maxDp;
    /* 0x0C */ u_short currentPp;
    /* 0x0E */ u_short maxPp;
    /* 0x10 */ char damageType;
    /* 0x11 */ char costType;
    /* 0x12 */ char cost;
    /* 0x13 */ char material;
    /* 0x14 */ char unk14;
    /* 0x15 */ char gemSlots;
    /* 0x16 */ char gemEffects;
    /* 0x17 */ char index;
    /* 0x18 */ vs_battle_range_t range;
    /* 0x1C */ signed char types[4];
    /* 0x20 */ signed char classes[8];
    /* 0x28 */ signed char affinities[8];
    /* 0x30 */
} vs_battle_uiEquipment;

typedef struct {
    /* 0x000 */ char name[24];
    /* 0x018 */ vs_battle_uiEquipment blade;
    /* 0x048 */ vs_battle_uiEquipment grip;
    /* 0x078 */ vs_battle_uiEquipment gems[3];
    /* 0x108 */ char wepId;
    /* 0x109 */ char damageTypeValue;
    /* 0x10A */ char risk;
    /* 0x10B */ char unk10B;
    /* 0x10C */ char dropRate;
    /* 0x10D */ u_char index;
    /* 0x10E */ u_char damageType;
    /* 0x10F */ u_char actionType;
    /* 0x110 */ u_short currentPp;
    /* 0x112 */ u_short maxPp;
    /* 0x114 */ u_short currentDp;
    /* 0x116 */ u_short maxDp;
    /* 0x118 */ short currentStr;
    /* 0x11A */ u_short baseStr;
    /* 0x11C */ short currentInt;
    /* 0x11E */ u_short baseInt;
    /* 0x120 */ short currentAgility;
    /* 0x122 */ u_short baseAgility;
    /* 0x124 */ vs_battle_range_t range;
    /* 0x128 */ vs_battle_classAffinityCurrent classAffinityCurrent;
    /* 0x168 */ short unk168[8];
    /* 0x178 */ vs_battle_classAffinityBaseline classAffinityBaseline;
    /* 0x198 */
} vs_battle_uiWeapon;

typedef struct {
    /* 0x000 */ int unk0[6];
    /* 0x018 */ vs_battle_uiEquipment base;
    /* 0x048 */ vs_battle_uiEquipment gems[3];
    /* 0x0D8 */ char wepId;
    /* 0x0D9 */ char dropRate;
    /* 0x0DA */ u_char index;
    /* 0x0DB */ char unkDB;
    /* 0x0DC */ u_short currentPp;
    /* 0x0DE */ u_short maxPp;
    /* 0x0E0 */ u_short currentDp;
    /* 0x0E2 */ u_short maxDp;
    /* 0x0E4 */ short currentStr;
    /* 0x0E6 */ u_short baseStr;
    /* 0x0E8 */ short currentInt;
    /* 0x0EA */ u_short baseInt;
    /* 0x0EC */ short currentAgility;
    /* 0x0EE */ u_short baseAgility;
    /* 0x0F0 */ char types[4];
    /* 0x0F4 */ vs_battle_classAffinityCurrent classAffinityCurrent;
    /* 0x134 */ short unk134[8];
    /* 0x144 */ vs_battle_classAffinityBaseline classAffinityBaseline;
    /* 0x164 */
} vs_battle_uiShield;

typedef struct {
    /* 0x00 */ vs_battle_uiEquipment armor;
    /* 0x30 */ u_short currentDp;
    /* 0x32 */ u_short maxDp;
    /* 0x34 */ short currentStr;
    /* 0x36 */ u_short baseStr;
    /* 0x38 */ short currentInt;
    /* 0x3A */ u_short baseInt;
    /* 0x3C */ short currentAgility;
    /* 0x3E */ u_short baseAgility;
    /* 0x40 */ u_short types[4];
    /* 0x48 */ vs_battle_classAffinityCurrent classAffinityCurrent;
    /* 0x88 */ short unk88[8];
    /* 0x98 */ char dropRate;
    /* 0x99 */ char unk99;
    /* 0x9A */ u_char index;
    /* 0x9B */ u_char unk9B;
    /* 0x9C */ vs_battle_classAffinityBaseline classAffinityBaseline;
    /* 0xBC */
} vs_battle_uiArmor;

typedef struct {
    /* 0x00 */ short hp;
    /* 0x02 */ short maxHp;
    /* 0x04 */ signed char agilityDefenseBonus;
    /* 0x05 */ char anchorBone;
    /* 0x06 */ u_char nameIndex;
    /* 0x07 */ char chainEvasion;
    /* 0x08 */ short types[4];
    /* 0x10 */ short affinities[8];
    /* 0x20 */ vs_battle_uiArmor armor;
    /* 0xDC */
} vs_battle_uiEquipment_limb;

typedef struct {
    /* 0x00 */ vs_battle_uiEquipment accessory;
    /* 0x30 */ short currentStr;
    /* 0x32 */ short currentInt;
    /* 0x34 */ short currentAgility;
    /* 0x36 */ char dropRate;
    /* 0x37 */ u_char index;
    /* 0x38 */ u_short types[4];
    /* 0x40 */ u_short classes[8];
    /* 0x50 */ u_short affinities[8];
    /* 0x60 */
} vs_battle_uiAccessory;

typedef struct {
    u_short id;
    char count;
    char unk3;
} vs_battle_uiMisc;

typedef struct {
    u_short id;
    char dropRate;
    char unk3;
} vs_battle_uiMiscRand;

typedef struct {
    char unk0[sizeof(vs_battle_wepModels_t)]; /* a weapon model slot */
} vs_battle_actor4;

typedef struct {
    short id;
    char index : 4;
    char unk2_4 : 4;
    char unk3;
} vs_battle_actor_unk8C0;

typedef struct {
    u_short unk0;
    char unk2_0 : 1;
    char unk2_1 : 1;
    char unk2_2 : 6;
    u_char unk3;
} vs_battle_actor2_flags;

/*
Flags for statuses
0x00000020 str down
0x00000040 str up
0x00000080 int down
0x00000100 int up
0x00000200 agi down
0x00000400 agi up
0x00000800 quicken
0x00001000 silent
0x00002000 paralysis
0x00004000 poison
0x00008000 numbness
0x00010000 curse
0x00020000 Regenaration
0x00040000 magic ward
0x00080000 equip down
0x00100000 equip up
0x00200000 air atk up
0x00400000 fire atk up
0x00800000 earth atk up
0x01000000 water atk up
0x02000000 air def up
0x04000000 fire def up
0x08000000 earth def up
0x10000000 water def up
*/

enum vs_battle_statuses {
    vsBattleStatusStrDown = 1 << 0,
    vsBattleStatusStrUp = 1 << 1,
    vsBattleStatusIntDown = 1 << 2,
    vsBattleStatusIntUp = 1 << 3,
    vsBattleStatusAgiDown = 1 << 4,
    vsBattleStatusAgiUp = 1 << 5,
    vsBattleStatusQuicken = 1 << 6,
    vsBattleStatusSilent = 1 << 7,
    vsBattleStatusParalysis = 1 << 8,
    vsBattleStatusPoison = 1 << 9,
    vsBattleStatusNumbness = 1 << 10,
    vsBattleStatusCurse = 1 << 11,
    vsBattleStatusRegen = 1 << 12,
    vsBattleStatusWard = 1 << 13,
    vsBattleStatusEquDown = 1 << 14,
    vsBattleStatusEquUp = 1 << 15,
    vsBattleStatusAirAtkUp = 1 << 16,
    vsBattleStatusFireAtkUp = 1 << 17,
    vsBattleStatusEarthAtkUp = 1 << 18,
    vsBattleStatusWaterAtkUp = 1 << 19,
    vsBattleStatusAirDefUp = 1 << 20,
    vsBattleStatusFireDefUp = 1 << 21,
    vsBattleStatusEarthDefUp = 1 << 22,
    vsBattleStatusWaterDefUp = 1 << 23
};

typedef struct {
    /* 0x000 */ char name[24];
    /* 0x018 */ short currentHP;
    /* 0x01A */ short maxHP;
    /* 0x01C */ short currentMP;
    /* 0x01E */ short maxMP;
    /* 0x020 */ u_short risk;
    /* 0x022 */ u_short strength;
    /* 0x024 */ u_short totalStrength;
    /* 0x026 */ u_short intelligence;
    /* 0x028 */ u_short totalIntelligence;
    /* 0x02A */ u_short agility;
    /* 0x02C */ u_short totalAgility;
    /* 0x02E */ char unk2E;
    /* 0x02F */ char unk2F;
    /* 0x030 */ char unk30;
    /* 0x031 */ char unk31;
    /* 0x032 */ char unk32;
    /* 0x033 */ char unk33;
    /* 0x034 */ u_int unk34 : 8;
    /* 0x035 */ u_int unk35 : 8;
    /* 0x036 */ u_int unk36 : 8;
    /* 0x037 */ u_int enemyClass : 3;
    /* 0x037 */ u_int reach : 5;
    /* 0x038 */ u_int currentRange : 8;
    /* 0x039 */ u_int unk39 : 8;
    /* 0x03A */ u_int unk3A : 8;
    /* 0x03B */ u_int unk3B_0 : 3;
    /* 0x03B */ u_int currentAttackShapeAngle : 5;
    /* 0x03C */ vs_battle_uiWeapon weapon;
    /* 0x1D4 */ vs_battle_uiShield shield;
    /* 0x338 */ vs_battle_uiAccessory accessory;
    /* 0x398 */ vs_battle_uiEquipment_limb limbs[6];
    /* 0x8C0 */ vs_battle_actor_unk8C0 armor[6][4];
    /* 0x920 */ char dpSplash[6][6];
    /* 0x944 */ int statusImmunities;
    /* 0x948 */ int statuses;
    /* 0x94C */ u_char statusTimers[8];
    /* 0x954 */ u_short actionRecoveryTimer;
    /* 0x956 */ u_char unk956_0 : 1;
    /* 0x956 */ u_char isAnalyzed : 1;
    /* 0x956 */ u_char unk956_2 : 6;
    /* 0x957 */ u_char actorId;
    /* 0x958 */ vs_battle_uiMisc miscItem;
    /* 0x95C */ int unk95C;
    /* 0x960 */ vs_battle_uiMiscRand unk960;
    /* 0x964 */
} vs_battle_actor2;

typedef struct vs_battle_actor {
    /* 0x00 */ struct vs_battle_actor* next;
    /* 0x04 */ int id;
    /* 0x08 */ u_char unk8;
    /* 0x09 */ u_char unk9;
    /* 0x0A */ u_char unkA;
    /* 0x0B */ u_char unkB;
    /* 0x0C */ int unkC;
    union {
        u_char u8[4];
        int s32;
    /* 0x10 */ } unk10;
    int unk14;
    u_short unk18;
    u_short subType;
    u_short unk1C;
    u_char unk1E;
    u_char unk1F;
    int weaponDrawn;
    u_char unk24;
    u_char equippedWeaponCategory;
    u_char unk26;
    u_char unk27;
    u_char defeated;
    u_char unk29;
    u_char unk2A;
    u_char unk2B;
    union {
        u_char u8[4];
        int s32;
    } unk2C;
    union {
        u_int raw;
        u_short half[2];
        u_char byte[4];
        struct {
            u_short unk30_0 : 4;
            u_short unk30_4 : 5;
            u_int unk30_9 : 5;
            u_int unk30_14 : 18;
        } bits;
    } flags30;
    int unk34;
    int unk38;
    vs_battle_actor2* unk3C;
    int unk40;
    vs_actors_ModelInstance* unk44;
    vs_battle_actor4* unk48[2];
} vs_battle_actor;

typedef struct {
    struct {
        vs_battle_actor unk0;
        vs_battle_actor2 unk50;
    } unk0;
    vs_actors_ModelInstance model;
    MATRIX unk6C[8];
    int unk16C;
    int unk170;
    int unk174;
} vs_battle_actor_dat2;

typedef struct {
    vs_battle_actor_dat2 unk0;
    /* the rest of the object: its room runs from unk9B4 */
    char unkB2C[VS_OBJECT_REGION - (sizeof(vs_battle_actor_dat2) - __builtin_offsetof(vs_battle_actor_dat2, model))];
    vs_battle_actor4 modelSlots[2];
} vs_battle_actor_dat;

typedef struct {
    char id;
    char exit;
    char flags;
    char lock;
} vs_battle_door;

typedef struct {
    char p0;
    char p1;
    char p2;
    char p3;
} vs_battle_prim;

typedef struct {
    int vertexCount;
    SVECTOR vertices[0];
} vs_battle_roomVertices;

typedef struct {
    char name[32];
    short prev;
    short next;
} vs_battle_roomName;

typedef struct {
    u_short currentManual;
    u_short unk2;
    u_short scrollPositions[14];
    u_short unk20;
    char unk22;
    char unk23;
} vs_battle_manualDisplayState_t;

typedef struct {
    char unk0[0x464];
} _zndEnemy;

typedef struct {
    u_char unk0;
    u_char unk1;
    u_char unk2;
    u_char unk3;
    int unk4;
    int unk8;
    int unkC;
    int unk10;
    int unk14;
    int unk18;
    int unk1C;
    int unk20;
    int unk24;
} _mpdEnemy;

typedef struct {
    int zndId;
    vs_main_CdFile* mpdLbas;
    int zndEnemyCount;
    _zndEnemy* zndEnemies;
    vs_main_CdFile* zudFiles;
    int loadedZudIndexPlusOne;
    vs_main_CdQueueSlot* zudDiscRequest;
    void* zudData;
    vs_main_CdQueueSlot* zndCdFile;
    void* zndData;
    int musicId;
    int musicSlot;
    int unk30;
    int mapId;
    int unk38;
    short tileColumns;
    short tileRows;
    _zoneContextBounds bounds;
    int unk48;
    int unk4C;
    int unk50;
    int unk54;
    vs_main_CdQueueSlot* mpdCdFile;
    void* mpdData;
    u_int mpdEnemyCount;
    _mpdEnemy* mpdEnemies;
    int currentSceneId;
} _zoneContext_t;

typedef struct {
    int x;
    int y;
    int unk8;
    int unkC;
} vs_render_FramebufferEffectTranslation;

typedef struct {
    int yaw;
    int pitch;
    int distance;
    int mode;
} _sphericalValues;

typedef struct {
    _sphericalValues values;
    _sphericalValues delta;
    int initialDistance;
    int unk24;
    int speed;
} _sphericalCamera;

typedef struct {
    short unk0;
    short unk2;
    short unk4;
    short unk6;
    union {
        char u8[2];
        u_short u16;
    } unk8;
    char unkA;
    char unkB;
    u_short unkC;
    char unkE;
    char displayTextLen;
    short unk10;
    short unk12;
    char displayText[4];
} D_800F1BAC_t;

typedef struct {
    vs_battle_uiEquipment blade;
    vs_battle_uiEquipment grip;
    vs_battle_uiEquipment gems[3];
    char material;
    char dropRate;
    short index;
    char name[24]; /* vs_string */
} vs_battle_weaponIntermediate;

typedef struct {
    vs_battle_uiEquipment base;
    vs_battle_uiEquipment gems[3];
    char material;
    char dropRate;
    short index;
} vs_battle_shieldIntermediate;

typedef struct {
    u_char titlePage;
    u_char titleRow;
    u_char encyclopaediaPage;
    u_char encyclopaediaRow;
    u_char recordTimePage;
    u_char recordTimeRow;
    u_char gazettePage;
    u_char gazetteRow;
} vs_battle_menu9CursorMemory_t;

typedef struct vs_battle_lootListNode {
    struct vs_battle_lootListNode* next;
    int actorId;
    vs_battle_loot loot;
} vs_battle_lootListNode;


typedef struct {
    vs_camera_t fromCameraState;
    _sphericalCamera fromSpherical;
    int fromNearClip;
    int fromProjectionDistance;
    int fromFarClip;
    _sphericalCamera toSpherical;
    int toNearClip;
    int toProjectionDistance;
    int toFarClip;
    VECTOR targetPosition;
    VECTOR targetLookAt;
    VECTOR direction;
    VECTOR lookAtStep;
    int farClipStep;
    int nearClipStep;
    int projectionDistanceStep;
    int transitionSpeed;
    int transitionFrames;
} vs_battle_cameraTransition_t;

typedef struct {
    vs_battle_uiEquipment equip;
    char material;
    char dropRate;
    char unk32;
    char index;
} _armorIntermediate;

typedef struct {
    vs_battle_uiEquipment equip;
    char dropRate;
    char index;
    char unk32;
    char unk33;
} _accessoryIntermediate;

typedef struct {
    u_short id;
    char unk2;
    char index;
} _actions;

typedef struct {
    u_short hp;
    char agilityDefenseBonus;
    char chainEvasion;
    signed char types[4];
    signed char affinities[8];
    _actions actions[4];
    _armorIntermediate armor;
    char dpSplash[6];
    char unk5A[2];
} _limbInit;

typedef struct {
    u_short subType;
    char characterKind;
    u_int reach : 5;
    u_int unk3_5 : 3;
    char name[24];
    short hp;
    short mp;
    char strength;
    char intelligence;
    char agility;
    char unk23;
    char unk24;
    char walkSpeed;
    char unk26;
    char runSpeed;
    int unk28;
    int unk2C;
    char unk30;
    char unk31;
    char unk32;
    char unk33;
    vs_battle_weaponIntermediate weapon;
    vs_battle_shieldIntermediate shield;
    _accessoryIntermediate accessory;
    _limbInit limbs[6];
    int mpdIdentifer;
} vs_battle_charInitData;

typedef struct {
    int unk0;
    short unk4;
    short unk6;
} func_8006CE70_t;

void vs_battle_armDefenseAbilityTimingWindow(int);
void func_8006CDB8(int);
void vs_battle_applyActionSourceEffects(int, func_8006CE70_t*);
void vs_battle_applyActionTargetEffects(int, func_8006CE70_t*);
void vs_actors_rememberEnemy(int, int);
void vs_battle_copyInventoryBladeStats(vs_battle_uiEquipment*, vs_main_inventoryBlade*);
void vs_battle_copyInventoryGripStats(vs_battle_uiEquipment*, vs_main_inventoryGrip*);
void vs_battle_copyInventoryArmorStats(vs_battle_uiEquipment*, vs_main_inventoryArmor*);
void vs_battle_copyInventoryGemStats(vs_battle_uiEquipment*, vs_main_inventoryGem*);
void vs_battle_applyWeapon(vs_battle_uiWeapon*, vs_main_inventoryWeapon*);
void vs_battle_applyShield(vs_battle_uiShield*, vs_main_inventoryShield*);
void vs_battle_applyArmor(vs_battle_uiArmor*, vs_main_inventoryArmor*);
void vs_battle_applyAccessory(vs_battle_uiAccessory* arg0, vs_main_inventoryArmor* arg1);
void vs_battle_equipWeapon(vs_main_inventoryWeapon*);
void vs_battle_equipShield(vs_main_inventoryShield* shield);
void vs_battle_equipArmor(int part, vs_main_inventoryArmor* armor);
void vs_battle_equipAccessory(vs_main_inventoryArmor* accessory);
void vs_battle_applyShieldStats(vs_battle_uiShield*, vs_battle_shieldIntermediate*);
void vs_battle_applyWeaponStats(vs_battle_uiWeapon*, vs_battle_weaponIntermediate*);
void vs_battle_copyUiBladeStats(
    vs_main_inventoryBlade* dropBlade, vs_battle_uiEquipment* targetBlade);
void vs_battle_copyUiGripStats(vs_main_inventoryGrip*, vs_battle_uiEquipment*);
void vs_battle_copyUiGemStats(vs_main_inventoryGem*, vs_battle_uiEquipment*);
void vs_battle_copyUiWeaponStats(vs_main_inventoryWeapon* arg0, vs_battle_uiWeapon* arg1);
void vs_battle_copyUiShieldStats(vs_main_inventoryShield*, vs_battle_uiShield*);
void vs_battle_copyUiArmorStats(vs_main_inventoryArmor*, vs_battle_uiArmor*);
void vs_battle_copyUiAccessoryStats(vs_main_inventoryArmor*, vs_battle_uiAccessory*);
void vs_battle_nop0(vs_battle_actor2*);
void func_8006C480(int, int);
void func_8006C4A4(int, int);
int func_8006C84C(int);
void vs_field_disableTrapAtPosition(func_8006CE70_t*);
void func_8006CE50(void);
void vs_field_applyUnlockEffectToRoomObject(func_8006CE70_t*);
int vs_battle_getCameraRoll(void);
void vs_battle_lookAt(VECTOR* from, VECTOR* to, MATRIX* mat);

/**
 * Retrieves room ID, only used for teleporting.
 *
 * @return Unique room ID from 0-511.
 */
int vs_battle_getCurrentRoomId(void);

void vs_render_configureFieldDisplay(int arg0, int arg1, int arg2, int arg3, int arg4, int arg5);
void vs_actors_initializeActorFromTemplate(
    int id, vs_battle_charInitData* arg1, int arg2, int arg3, int arg4, int arg5);
vs_battle_actor* vs_actors_createActorWithEquipment(int arg0, int arg1, int bladeWepId, int bladeMaterial,
    int shieldWepId, int shieldMaterial, vs_battle_objectData_flags* arg6, int arg7);
void vs_field_resetCameraToAshley(void);
int vs_battle_syncCameraAnglesFromPosition(_sphericalCamera*);
int func_8007BD8C(int);
void vs_battle_setCameraRoll(int arg0);
void vs_render_buildCameraViewMatrix(void);
void func_8007B0FC(void);
void func_8007B104(void);
void vs_render_transformSceneColors(int, int, short, short, short);
void vs_render_transformActorColors(int, int, int, short, short, short);
void vs_render_transformBackgroundColors(int, int, short, short, short);
void vs_battle_beginEventRewardResults(void);
void vs_battle_beginPostActionRewards(void);
int vs_battle_updateRewardSequence(void);
void vs_items_presentScriptedLoot(int, int, int, int);
void func_8007BBB8(int, int);
void vs_state_startSceneTimer(int, int, int);
void vs_field_cancelPendingRoomVisit(void);
int func_8007C8F8(int);
void vs_field_setCameraPitchPreset(int);
int vs_actors_countActorsByDisposition(int);
void vs_state_resetSceneTimer(void);
void vs_state_setSceneTimerPaused(int);
int vs_field_setFirstPersonViewDisabled(int);
int vs_audio_setZoneMusicSwitchEnabled(int);
void func_8007BE20(int, int);
void func_8007BEC4(int);
int func_8007BEE4(void);
void func_8007BF04(void);
int func_8007BF0C(void);
void func_8007BF2C(void);
void func_8007BF4C(void);
void vs_actors_enableAllActorModels(void);
void vs_actors_disableAllActorModels(void);
void vs_battle_setActorBattleMode(int, int);
void vs_actors_restoreActorVitals(int);
void vs_battle_preserveAshleyStatuses(void);
void func_8007C348(int, int);
int vs_state_selectTickSpeed(int arg0);
int vs_field_screenTransitionPhase(void);
void func_8007C404(void);
void func_8007C424(void);
void vs_field_configureScreenTransition(int, int, int);
void vs_field_setTransitionWipeAngle(int);
void vs_field_beginScreenFadeOut(int, int, int);
void vs_field_beginScreenFadeIn(int, int, int);
void vs_field_beginScriptedRoomTransition(_mpdRoomDoorSection_t*, int, int);
void vs_field_beginScriptedZoneTransition(_mpdRoomDoorSection_t*, int, int);
void vs_audio_playActorScriptSound(int, int);
int vs_actors_queueScriptedActorSpawn(u_int arg0, int arg1, vs_battle_objectData_flags* arg2);
void vs_actors_loadActorEquipmentModels(int, int, int);
void vs_actors_refreshActorEquipmentModels(int);
void vs_actors_queueSharedSequenceLoad(int, int wepId);
void vs_actors_unloadSharedSequence(int);
void vs_actors_queueSharedSequenceWithTextures(int, int, int);
void vs_battle_loadEtm(int, int, int);
void vs_actors_releaseSequenceEffectTextures(int);
void vs_actors_setSharedSequenceBank(int);
int func_8007CCAC(void);
void vs_battle_setFarClip(int arg0, int arg1);
void vs_battle_initialiseCameraFromSpherical(VECTOR*, VECTOR*, int, int);
int vs_actors_findActorByModelId(int);
vs_battle_actor* vs_actors_isBattleActorSlot(int actorId);
void func_8007CFF8(int, SVECTOR*);
int vs_actors_isScriptedActorPathFinished(int);
int vs_actors_setActorLogicEnabled(int, int);
int vs_actors_setOtherActorLogicEnabled(int);
void vs_actors_setActorDetached(int, int);
int vs_battle_isActionUnlocked(u_int);
void vs_battle_setActionUnlocked(u_int);
void vs_actors_removeActorAtSlot(int);
void func_8007D3F8(void);
void func_8007DF48(void);
void vs_battle_setProjectionDistance(int);
void vs_battle_getCameraPosition(VECTOR* outPosition);
void vs_battle_setCameraPosition(VECTOR* inPosition);
void vs_battle_getCameraLookAt(VECTOR* outLookAt);
void vs_battle_setCameraLookAt(VECTOR* inLookAt);
void vs_battle_setNearClip(int arg0);
void vs_render_setFramebufferEffectEnabled(int);
void vs_render_setFramebufferEffectRoll(int arg0);
void vs_render_setFramebufferEffectScale(vs_render_FramebufferEffectTranslation* arg0);
void vs_render_setFramebufferEffectTint(P_CODE* arg0);
void vs_render_setFramebufferEffectOffset(vs_render_FramebufferEffectTranslation* arg0);
void vs_render_setFramebufferEffectBlendMode(int arg0);
void vs_render_setFramebufferEffectDepth(u_int arg0);
void vs_render_setFramebufferEffectPreviousFrame(int arg0);
int vs_render_reserveTexturePages(u_int, u_int, int);
int vs_render_retireTexturePages(u_int, u_int, int);
int func_8007E180(int arg0);
int func_8007E1C0(int arg0);
int vs_battle_itemIdIsInInventory(int);
void vs_battle_inflictActorStatuses(int, vs_battle_actor2*);
void vs_battle_setDoorEntered(void);
int func_80089104(void);
void vs_field_setFieldRenderingEnabled(int arg0);
int vs_field_currentSavePointId(void);
void vs_items_clearRoomLootList(void);

/**
 * Retrieve action availability info.
 *
 * @return Packed value
 * - Bit 0:
 * - Bit 1: Requirements not met (i.e. insufficient HP/MP/Risk)
 * - Bit 2: action locked
 */
int vs_battle_getActionFlags(int actorId, int actionId);
void vs_field_setRoomTint(D_800F4538_unk58_2*, int);
_mpdRoomSection3* vs_field_collisionCellAt(u_int arg0, u_int arg1, int arg2);
void vs_battle_setRoomsUnk0(vs_battle_scene*);
void vs_field_resetGimmickAnimationQueue(void);
int func_8008B808(int, int, int, int);
int vs_battle_getCurrentSceneId(void);
int vs_battle_inWorkshop(void);
void vs_battle_setDoorEntered2(int arg0);
void vs_field_clearGeometryOcclusion(void);
void vs_field_setMovingPlatformsPaused(int);
void vs_field_freezeMovingPlatforms(int);
void vs_battle_parseTim(u_long*, TIM_IMAGE*);
int vs_battle_getMapCompletion(void);
void vs_field_setMovingPlatformVisibility(int, int);
int vs_field_isRoomAudioSequenceAbsent(void);
int vs_field_roomSigilDoorUnlockByte(int, int, u_int);
void vs_field_initializeRoomBackground(void);
void vs_field_setBackgroundParallaxScale(int, int);
void vs_field_setTiledBackgroundEnabled(int);
int* vs_field_roomBackgroundColors(void);
void vs_field_setRoomBackgroundColors(int*);
int vs_field_queueTiledBackgroundLoad(int);
int vs_field_finishTiledBackgroundLoad(void);
int vs_field_releaseTiledBackground(void);
void vs_battle_exec(void);

extern int vs_battle_mapCompletionFlags[];
extern char vs_battle_loadObjectDataState;
extern _zoneContext_t _zoneContext;
extern vs_battle_lootListNode* _lootListHead;
extern vs_battle_actor* vs_battle_actors[];
extern _sphericalCamera vs_battle_cameraCurrentSpherical;
extern vs_battle_actor* vs_battle_characterState;
extern _mpdRoomDoorSection_t vs_field_pendingDestinationDoor;
extern u_short vs_field_areRoomUpdatesPaused;
extern u_short* vs_field_roomPalettes;
extern D_800F1BAC_t (*vs_battle_floatingLabels)[32];
extern short vs_field_isRoomTintActive;
extern short vs_battle_doorEntered;
extern void* vs_battle_sceneBuffer;
extern vs_battle_manualDisplayState_t vs_battle_manualDisplayState;
extern vs_battle_menu9CursorMemory_t vs_battle_menu9CursorMemory;
extern _mpdRoomSection vs_battle_roomData;
extern void* vs_battle_placedLabelPositions;
extern _mpdRoomDoorSection_t vs_field_entranceDoorRecord;
extern short vs_field_minimapSlideFrame;
extern char vs_field_isMinimapVisible;
extern int vs_battle_submenuState;
extern int D_800F1CE0[8];
extern int vs_battle_screenTransitionAlpha;
extern vs_battle_cameraTransition_t* vs_battle_cameraTransition;
extern u_int vs_battle_screenTransitionStep;
extern int vs_battle_screenTransitionEffect;
extern int vs_battle_screenTransitionWipeAngle;
extern int vs_battle_screenTransitionSpeed;
extern void* D_800F4930;
