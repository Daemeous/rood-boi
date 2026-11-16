#pragma once
#include "146C.h"
#include "3A1A0.h"
#include <libgte.h>
#include "pc/src/psx_types.h"

typedef struct vs_battle_aiState vs_battle_aiState;

/* The AI's record of one actor (0x484 bytes, one per battle actor, vs_battle_aiActorAiStates). The game's functions each
 * read it as their own phase; the unions are those phases, the fields named for what that phase calls them. */

typedef struct {
    int score;
    unsigned int row : 3, column : 2, action : 8, threshold : 16, pad29 : 3;
    unsigned int distanceSquared;
    unsigned int targetId : 4, part : 3, heading : 12, weight : 8, padC27 : 5;
    unsigned int low : 1, flag1 : 1, flag2 : 1, flag3 : 1, flag4 : 1, flag5 : 1,
        range : 16, high : 10;
} actionCandidateEntry;

typedef struct {
    unsigned char flags, pad[3];
} actionCandidateTarget;

typedef struct {
    u_int flag0 : 1;
    u_int flag1 : 1;
    u_int flag2 : 1;
    u_int flag3 : 1;
    u_int unk4 : 28;
} actionChoiceFlags;

typedef struct {
    int unk0;
    u_int category : 3;
    u_int slot : 2;
    u_int action : 8;
    u_int unk4_13 : 19;
    u_int score;
    u_int unkC_0 : 19;
    u_int targetWeight : 8;
    u_int unkC_27 : 5;
    actionChoiceFlags flags;
} actionChoice;

typedef struct {
    unsigned int pad0, flags4, distance, flagsC;
} actionDispatchEntry;

typedef struct {
    unsigned int f0 : 1, f1 : 1, f2 : 1, kind : 2, longRange : 1, special : 1, f7 : 1,
        radius : 16, high : 8;
    unsigned int rangeSquared;
} actionMetadataEntry;

typedef struct {
    unsigned short id, pad;
} actionMetadataSlot;

typedef struct {
    char pad[0x8C0];
    actionMetadataSlot slots[6][4];
} actionMetadataStats;

typedef union {
    unsigned int raw;
    struct {
        unsigned int unk0 : 6, classification : 2, unk8 : 7, timer : 5, counter : 7,
            unk27 : 1, previous : 2, unk30 : 2;
    } p;
    struct {
        unsigned char flags, pad[3];
    } b;
} actionReactionTarget;

typedef struct {
    u_int unk0_0 : 2;
    u_int flag2 : 1;
    u_int mode : 2;
    u_int flag5 : 1;
    u_int usable : 1;
    u_int enabled : 1;
    u_int range : 16;
    u_int unk0_24 : 8;
    u_int value;
} actionSlotConfig;

typedef union {
    unsigned int raw;
    struct {
        unsigned char b0, b1, b2, b3;
    } bytes;
    struct {
        unsigned int low : 24, f24 : 2, f26 : 2, high : 4;
    } bits;
} actorInitializationFlags;

typedef struct actorInitializationStats {
    char pad0[48];
    unsigned char field30;
    unsigned char field31;
    unsigned char field32;
    unsigned char field33;
} actorInitializationStats;

typedef union {
    unsigned int raw;
    struct {
        unsigned int x : 8, y : 8, z : 8, status : 8;
    } p;
} vs_battle_movementPosition;

typedef struct {
    char unk0[8];
    union {
        u_int raw;
        struct {
            u_int unk0 : 16;
            u_int unk16 : 3;
            u_int unk19 : 2;
            u_int unk21 : 10;
            u_int unk31 : 1;
        } bits;
    } flags;
    char unkC[8];
    short unk14;
    char unk16[6];
    SVECTOR position;
    short unk24;
    short facing;
    char unk28[0x34];
    vs_battle_movementPosition tile;
    vs_battle_movementPosition lastTile;
} actorObject;

typedef union {
    u_int raw;
    struct {
        u_int active : 1;
        u_int previous : 1;
        u_int hidden : 1;
        u_int inView : 1;
        u_int unk4 : 1;
        u_int visible : 1;
        u_int kind : 2;
        u_int counter : 7;
        u_int timer : 5;
        u_int kindTimer : 7;
        u_int blocked : 1;
        u_int previousKind : 2;
        u_int outOfReach : 1;
        u_int unk31 : 1;
    } bits;
} actorRelation;

typedef union {
    unsigned int raw;
    unsigned short half[2];
    unsigned char byte[4];
} actorSettingsWord;

typedef struct {
    u_int position;
    u_int unk4;
} actorUpdatePoint;

typedef union {
    unsigned int raw;
    unsigned char bytes[4];
} candidateMovementWord;

typedef struct {
    unsigned int unk0, unk4, distance, flagsC;
} da4bcEntry;

typedef struct {
    unsigned int pad0 : 6, mode : 2, pad8 : 12, category : 7, pad27 : 1, oldMode : 2,
        pad30 : 2;
} da4bcTargetBits;

typedef union {
    unsigned int raw;
    struct {
        unsigned char x, y, z, w;
    } p;
} directionalCachePoint;

typedef struct {
    int unk0;
    u_int unk4_0 : 3;
    u_int unk4_3 : 2;
    u_int unk4_5 : 8;
    u_int unk4_13 : 19;
    int unk8;
    u_int unkC_0 : 4;
    u_int unkC_4 : 3;
    u_int unkC_7 : 12;
    u_int unkC_19 : 8;
    u_int unkC_27 : 5;
} func_800DCAA0_t;

typedef struct {
    u_char unk0;
    u_char unk1;
    u_char unk2;
} func_800DFA54_t;

typedef struct {
    int unk0;
    int unk4;
    u_int unk8_0 : 21;
    u_int unk8_21 : 1;
    u_int unk8_22 : 10;
} func_800E1238_t;

typedef struct {
    int unk0;
    u_int unk4;
} func_800E78F4_t2;

typedef struct {
    char pad0[8];
    unsigned int flags;
} movementCheckObject;

typedef union {
    u_int raw;
    struct {
        u_int unk0 : 1;
        u_int direction : 3;
        u_int clockwise : 1;
        u_int count : 7;
        u_int visited : 4;
        u_int stop : 1;
        u_int changed : 1;
        u_int cell : 4;
        u_int boundX : 5;
        u_int boundZ : 5;
    } bits;
} movementPath;

typedef struct {
    char pad0[8];
    unsigned int flags;
    char padC[8];
    short angle14;
    char pad16[0x10];
    short angle26;
} movementRecoveryContext;

typedef union {
    unsigned int raw;
    struct {
        short x;
        short y;
    } p;
} movementStepPosition;

typedef struct {
    short x, y, z, pad;
} obstructionVector;

typedef struct {
    char pad[0x1C];
    obstructionVector position;
} obstructionModel;

typedef union {
    u_int raw;
    struct {
        u_char x, y, z, status;
    } p;
} stuckMovementPosition;

typedef struct {
    unsigned int f0 : 1, f1 : 1, f2 : 1, pad3 : 2, f5 : 1, pad6 : 26;
} targetMovementFlags;

typedef struct {
    char pad0[8];
    unsigned int flags;
    char padC[16];
    short x;
    char pad1E[2];
    short z;
} targetMovementModel;

typedef union {
    unsigned int raw;
    unsigned char b[4];
} targetMovementPoint;

typedef union {
    unsigned int raw;
    struct {
        unsigned char x, y, z, w;
    } p;
} terrainMovementPoint;

typedef struct {
    char pad[0x18];
    short current, maximum;
} trackingMotionStats;

typedef struct {
    char pad0[8];
    unsigned int flags;
} wanderObject;

typedef struct {
    int unk0;
    u_short unk4;
} func_800E5A74_t;

typedef struct {
    char rangeX;
    char rangeY;
    char rangeZ;
    u_char shape : 3;
    u_char angle : 5;
} func_800E5568_t;

typedef struct {
    char unk0[0x38];
    func_800E5568_t unk38;
} func_800E5698_t2;

typedef struct {
    u_short unk0_0 : 5;
    u_short unk0_5 : 5;
    u_short unk0_10 : 6;
} D_800F5910_t;

typedef struct {
    short unk0;
    short unk1;
    short unk2;
    short unk3;
} func_8008D2C0_t;

typedef struct {
    signed char dx : 2;
    signed char unk0_2 : 2;
    signed char dz : 2;
    signed char unk0_6 : 2;
} D_800F16EC_t;

/* The room's navigation record (vs_battle_aiRoomAiState, one heap block built by buildNavigationGrid): the room's bounds and
 * encounter state, then the 32 x 32 cell flags and, 0x800 bytes on, their heights. */
typedef struct D_800F58BC_t {
    u_char maxX, maxZ, minX, minZ;
    u_short unk4;
    u_short unk6;
    u_short unk8;
    u_char unkA;
    u_char unkB;
    u_char unkC;
    u_char unkD; /* the most that can engage at once */
    u_char closedGates; /* the room flags (vs_battle_aiClearAiContactOccupancy / vs_battle_aiSetAiContactOccupancy) */
    u_char unkF;
    u_short unk10; /* the actors engaged, one bit each */
    u_char unk12; /* how many */
    u_char unk13;
    int unk14; /* the height limit */
    u_short unk18; /* the encounter clock */
    char unk1A[2];
    vs_battle_aiState* unk1C; /* the engaged list: first, last */
    vs_battle_aiState* unk20;
    int unk24;
    vs_battle_actor* registered[4];
    u_short cells[]; /* 0x400 cell flags; the heights are at cells + 0x400 */
} D_800F58BC_t;

typedef struct {
    char unk0[0xA];
    u_char unkA;
    char unkB;
    short unkC;
    short unkE;
    char unk10[2];
    u_short unk12;
    char unk14[2];
    u_char unk16;
    u_char unk17;
    char unk18[4];
    u_char unk1C;
} D_800F5920_t;

typedef struct {
    unsigned short first, second;
    unsigned char third, pad;
} actorSettingsEntry;

typedef union {
    int value;
    u_char b[4];
} aiWord;

typedef union {
    u_char b[8];
    int w[2];
} aiHistory;

typedef struct {
    u_char classification;
    char pad[3];
} actionScoreTarget;

typedef struct {
    u_char unk0;
    char unk1;
    u_char unk2;
    char unk3[0x3D];
    int unk40;
} func_800DEB10_t;

typedef struct {
    u_short unk0;
    char unk2[2];
    u_char unk4;
    char unk5[0x3F];
    int unk44;
    char unk48[2];
    u_short unk4A;
    func_800DEB10_t unk4C[0];
} func_800DEEA4_t;

typedef struct vs_battle_aiState {
    /* 0x0 */ union { u_int word; u_char b[4]; } flags0;
    /* 0x4 */ union { u_int word; u_char b[4]; } flags4;
    /* 0x8 */ union { u_int word; u_char b[4]; } flags8;
    /* 0xc */ union { u_short word; u_char b[2]; } flagsC;
    /* 0xe */ u_char flagE;
    /* 0xf */ u_char flagF;
    /* 0x10 */ u_char unk10;
    /* 0x11 */ u_char unk11;
    /* 0x12 */ u_char unk12;
    /* 0x13 */ u_char flag13;
    /* 0x14 */ u_char unk14;
    u_char unk15;
    /* 0x16 */ signed char target16;
    /* 0x17 */ u_char maxActionCost;
    /* 0x18 */ u_char unk18;
    /* 0x19 */ u_char f19;
    /* 0x1a */ u_char f1A;
    /* 0x1b */ u_char dead;
    /* 0x1c */ u_char unk1C;
    /* 0x1d */ u_char unk1D;
    char unk1E[0x2];
    /* 0x20 */ u_char flag20;
    /* 0x21 */ u_char f21;
    /* 0x22 */ u_char unk22;
    /* 0x23 */ u_char f23;
    /* 0x24 */ u_char f24;
    /* 0x25 */ u_char unk25;
    /* 0x26 */ u_char allowFallback;
    /* 0x27 */ u_char flag27;
    u_char unk28;
    /* 0x29 */ u_char f29;
    /* 0x2a */ u_char field2A;
    /* 0x2b */ u_char special;
    /* 0x2c */ u_char f2C;
    /* 0x2d */ u_char f2D;
    u_char unk2E;
    /* 0x2f */ u_char unk2F;
    /* 0x30 */ u_char f30;
    /* 0x31 */ u_char f31;
    /* 0x32 */ u_char f32;
    /* 0x33 */ u_char f33;
    /* 0x34 */ union { vs_battle_movementPosition position; directionalCachePoint cache; terrainMovementPoint terrain; stuckMovementPosition stuck; u_int raw; int sraw; u_char b[4]; } tile;
    /* 0x38 */ union { int previousView; aiWord aiWord; stuckMovementPosition stuckMovementPosition; directionalCachePoint directionalCachePoint; } previous;
    /* 0x3c */ u_int unk3C;
    /* 0x40 */ u_short unk40;
    /* 0x42 */ short options;
    /* 0x44 */ u_char timer44;
    /* 0x45 */ u_char unk45;
    /* 0x46 */ u_short unk46;
    /* 0x48 */ union { targetMovementPoint targetMovementPoint; vs_battle_movementPosition movementPosition; int target48; } previous48;
    /* 0x4c */ union { SVECTOR v; short xyz[3]; movementStepPosition step; obstructionVector obstruction; int word[2]; } position;
    /* 0x54 */ vs_battle_actor* actor;
    /* 0x58 */ union { actorObject* actorObject; func_800E1238_t* func_800E1238_t; movementCheckObject* movementCheckObject; D_800F4538_t* D_800F4538_t; movementRecoveryContext* movementRecoveryContext; targetMovementModel* targetMovementModel; wanderObject* wanderObject; obstructionModel* obstructionModel; } object;
    /* 0x5c */ union { vs_battle_actor2* actor2; actionMetadataStats* actionMetadataStats; actorInitializationStats* actorInitializationStats; actorUpdatePoint* actorUpdatePoint; trackingMotionStats* trackingMotionStats; } stats;
    /* 0x60 */ union { short* shorts; actorUpdatePoint* actorUpdatePoint; } position60;
    /* 0x64 */ union { u_int* words; actorUpdatePoint* actorUpdatePoint; } tilePointer;
    /* 0x68 */ void* callback;
    /* 0x6c */ void* savedCallback;
    /* 0x70 */ aiHistory historyX;
    /* 0x78 */ aiHistory historyZ;
    /* 0x80 */ u_char historyIndex;
    /* 0x81 */ u_char historyMask;
    /* 0x82 */ short height;
    char unk84[0x4];
    /* 0x88 */ u_char id;
    /* 0x89 */ u_char unk89;
    /* 0x8a */ u_char field8A;
    u_char unk8B;
    /* 0x8c */ u_short attempts;
    /* 0x8e */ u_short timer8E;
    /* 0x90 */ u_short unk90;
    /* 0x92 */ u_short radius;
    /* 0x94 */ u_short collisionRadius;
    char unk96[0x2];
    /* 0x98 */ u_short timer98;
    /* 0x9a */ u_short timer;
    /* 0x9c */ int field9C;
    char unkA0[0x4];
    /* 0xa4 */ u_int nearDistance;
    /* 0xa8 */ u_int angleLimit;
    /* 0xac */ u_int rangeSquared;
    /* 0xb0 */ u_short fB0;
    /* 0xb2 */ u_char radiusB2;
    /* 0xb3 */ u_char step;
    /* 0xb4 */ int distance;
    /* 0xb8 */ short extent;
    /* 0xba */ u_short delay;
    /* 0xbc */ short clearance;
    /* 0xbe */ short heightBE;
    /* 0xc0 */ u_short mode;
    /* 0xc2 */ u_short timerC2;
    /* 0xc4 */ int rangeSquaredC4;
    /* 0xc8 */ u_int fC8;
    /* 0xcc */ u_char fCC;
    /* 0xcd */ u_char fCD;
    /* 0xce */ u_char fCE;
    /* 0xcf */ u_char fCF;
    /* 0xd0 */ short angle;
    /* 0xd2 */ u_short cooldown;
    /* 0xd4 */ u_short restPeriod;
    /* 0xd6 */ u_short movePeriod;
    /* 0xd8 */ u_char rate;
    char unkD9[0x3];
    /* 0xdc */ int range;
    /* 0xe0 */ int previousRange;
    /* 0xe4 */ u_char bE4;
    /* 0xe5 */ u_char actorId;
    /* 0xe6 */ u_char timeout;
    /* 0xe7 */ u_char bE7;
    /* 0xe8 */ u_char bE8;
    /* 0xe9 */ u_char rangeMode;
    /* 0xea */ u_char previousOption;
    /* 0xeb */ u_char previousTargetId;
    /* 0xec */ targetMovementPoint cachedDestination;
    char unkF0[0x4];
    /* 0xf4 */ u_char targetTileX;
    u_char unkF5;
    /* 0xf6 */ u_char targetTileZ;
    u_char unkF7;
    /* 0xf8 */ u_short timerF8;
    /* 0xfa */ u_char detourStep;
    /* 0xfb */ u_char unkFB;
    char unkFC[0x4];
    /* 0x100 */ u_char index;
    /* 0x101 */ u_char row;
    /* 0x102 */ u_char rate102;
    /* 0x103 */ u_char direction;
    /* 0x104 */ u_char timer104;
    char unk105[0x3];
    /* 0x108 */ int previousDestination;
    /* 0x10c */ u_char pending;
    char unk10D[0x3];
    /* 0x110 */ int field110;
    /* 0x114 */ u_short unk114;
    char unk116[0x2];
    /* 0x118 */ u_char detourDirections[4];
    /* 0x11c */ vs_battle_movementPosition goal;
    /* 0x120 */ u_int unk120;
    /* 0x124 */ u_char unk124;
    /* 0x125 */ u_char unk125;
    /* 0x126 */ u_char unk126;
    /* 0x127 */ u_char unk127;
    /* 0x128 */ u_short angle128;
    /* 0x12a */ u_char unk12A;
    /* 0x12b */ u_char phase;
    u_char unk12C;
    /* 0x12d */ u_char mode12D;
    /* 0x12e */ u_char mode12E;
    /* 0x12f */ signed char phase12F;
    /* 0x130 */ signed char direction130;
    /* 0x131 */ u_char status;
    /* 0x132 */ signed char terrain132;
    u_char unk133;
    /* 0x134 */ candidateMovementWord destination;
    /* 0x138 */ u_short unk138;
    /* 0x13a */ u_short timer13A;
    /* 0x13c */ u_short duration;
    u_char unk13E;
    /* 0x13f */ u_char minDelay;
    /* 0x140 */ union { actionDispatchEntry fallback; actionChoice choice; } entry;
    /* 0x154 */ u_char area;
    u_char unk155;
    /* 0x156 */ u_short timer156;
    /* 0x158 */ short targetX;
    /* 0x15a */ short targetZ;
    /* 0x15c */ u_char movementTargetId;
    /* 0x15d */ u_char count;
    /* 0x15e */ u_short flags;
    /* 0x160 */ u_int unk160;
    char unk164[0x4];
    /* 0x168 */ union { u_int destination; directionalCachePoint directionalCachePoint; targetMovementPoint targetMovementPoint; terrainMovementPoint terrainMovementPoint; vs_battle_movementPosition movementPosition; candidateMovementWord candidateMovementWord; stuckMovementPosition stuckMovementPosition; } destination168;
    char unk16C[0x4];
    /* 0x170 */ union { int nextView; vs_battle_movementPosition movementPosition; } next;
    /* 0x174 */ u_int unk174;
    /* 0x178 */ u_short unk178;
    /* 0x17a */ u_short unk17A;
    /* 0x17c */ int moveX;
    /* 0x180 */ int moveZ;
    /* 0x184 */ int moveY;
    /* 0x188 */ short heading;
    /* 0x18a */ short angle18A;
    /* 0x18c */ struct vs_battle_aiState* route;
    /* 0x190 */ union { movementPath path; u_int raw; struct { u_int unk190_0 : 5; u_int unk190_5 : 7; u_int unk190_12 : 20; } update; } movementFlags;
    /* 0x194 */ int field194;
    u_char unk198;
    /* 0x199 */ u_char unk199;
    /* 0x19a */ u_char waitTimer;
    /* 0x19b */ u_char gate;
    /* 0x19c */ func_800DFA54_t exit;
    u_char unk19F;
    /* 0x1a0 */ struct vs_battle_aiState* next1A0;
    /* 0x1a4 */ union { struct { union { actorRelation relations[16]; targetMovementFlags movement[16]; actionCandidateTarget candidate[16]; candidateMovementWord word[16]; actionScoreTarget score[16]; actionReactionTarget reaction[16]; da4bcTargetBits da4bc[16]; u_int bits32[16]; } targets; u_int distances[16]; } t; u_int targetFlags[17]; u_char targetFlag0; } tbl;
    /* 0x224 */ u_char field224;
    /* 0x225 */ u_char field225;
    /* 0x226 */ u_char field226;
    /* 0x227 */ u_char field227;
    /* 0x228 */ int counter;
    /* 0x22c */ actionCandidateEntry* candidate;
    /* 0x230 */ union { actionSlotConfig actionSlotConfig[6][4]; actionMetadataEntry actionMetadataEntry[6][4]; } configs;
    /* 0x2f0 */ actionCandidateEntry candidates[16];
    /* 0x430 */ union { actionCandidateEntry* candidates[16]; actionDispatchEntry* dispatch[16]; da4bcEntry* da4bc[16]; func_800DCAA0_t* actions[16]; func_800E78F4_t2* first; } entries;
    /* 0x470 */ int count470;
    /* 0x474 */ u_short requests[6];
    u_char unk480;
    /* 0x481 */ u_char reaction;
    char unk482[0x2];
} vs_battle_aiState;
