#pragma once

typedef struct {
    char range;
    char unk1;
    char unk2;
    char attackShape : 3;
    char attackShapeAngle : 5;
} vs_battle_range_t;

typedef struct {
    u_char id;
    char subId;
    char wepId;
    char category;
    u_short maxDp;
    u_short maxPp;
    u_short currentDp;
    u_short currentPp;
    signed char strength;
    signed char intelligence;
    signed char agility;
    char cost;
    char damageType : 2;
    char costType : 3;
    char unk10_5 : 3;
    char unk11;
    u_short unk12;
    vs_battle_range_t range;
    signed char classes[8];
    signed char affinities[8];
    u_short material;
    char assembledWeaponIndex;
    char index;
} vs_main_inventoryBlade;

typedef struct {
    u_short id;
    char subId;
    u_char category;
    char gemSlots;
    signed char strength;
    signed char intelligence;
    signed char agility;
    signed char types[4];
    u_short assembledWeaponIndex;
    u_short index;
} vs_main_inventoryGrip;

typedef struct {
    u_short id;
    char subId;
    char unk3;
    char gemEffects;
    signed char strength;
    signed char intelligence;
    signed char agility;
    signed char classes[8];
    signed char affinities[8];
    u_short setItemIndex;
    short index;
} vs_main_inventoryGem;

typedef struct {
    char index;
    u_char blade;
    u_char grip;
    char isEquipped;
    u_char gems[4];
    char name[24];
} vs_main_inventoryWeapon;

typedef struct {
    u_char id;
    u_char subId;
    char wepId;
    char category;
    u_short maxDp;
    u_short maxPp;
    u_short currentDp;
    u_short currentPp;
    char gemSlots;
    signed char strength;
    signed char intelligence;
    signed char agility;
    signed char types[4]; // 0x10
    signed char classes[8];
    signed char affinities[8];
    u_short material;
    u_char limb;
    u_char index;
} vs_main_inventoryArmor;

typedef struct {
    char index;
    char isEquipped;
    short unk2;
    vs_main_inventoryArmor base;
    u_char gems[4]; // 0x2C
} vs_main_inventoryShield;

typedef struct {
    u_short id;
    char count;
    char index;
} vs_main_inventoryMisc;

typedef struct {
    vs_main_inventoryWeapon weapons[32];
    vs_main_inventoryShield shields[32];
    vs_main_inventoryBlade blades[64];
    vs_main_inventoryGrip grips[64];
    vs_main_inventoryArmor armor[64];
    vs_main_inventoryGem gems[192];
    vs_main_inventoryMisc misc[256];
    // 0x3C00
} vs_menu_containerData;

typedef struct {
    u_short weapons[32];
    u_short blades[64];
    u_short grips[64];
    u_short shields[32];
    u_short armor[64];
    u_short gems[192];
    u_short misc[256];
} vs_menu_containerIndices;

typedef struct {
    vs_menu_containerData data;
    vs_menu_containerIndices indices;
} vs_menu_container;

/* The bag's capacity per category. VS_BIG_INVENTORY (an optional mod) doubles the equipment:
 * the bag then lives in RAM the game never uses (vs_main_bigInventory, main.h), while saves
 * keep the original layout (vs_main_savedInventory_t) and the mod moves the second half
 * (src/BATTLE/BATTLE.PRG/mod/inventory.c). Without it every count is the original one. */
#ifdef VS_BIG_INVENTORY
#define VS_INVENTORY_SCALE 2
#else
#define VS_INVENTORY_SCALE 1
#endif
#define VS_INVENTORY_WEAPONS (8 * VS_INVENTORY_SCALE)
#define VS_INVENTORY_SHIELDS (8 * VS_INVENTORY_SCALE)
#define VS_INVENTORY_BLADES (16 * VS_INVENTORY_SCALE)
#define VS_INVENTORY_GRIPS (16 * VS_INVENTORY_SCALE)
#define VS_INVENTORY_ARMOR (16 * VS_INVENTORY_SCALE)
#define VS_INVENTORY_GEMS (48 * VS_INVENTORY_SCALE)
#define VS_INVENTORY_MISC 64
/* Where the bigger bag's extra half is saved (mod/inventory.h): VS_BIG_INVENTORY is
 * VS_BIG_INVENTORY_CONTAINER (all of it in the container's last slots, so any game that loads
 * the save finds those items in its container) or VS_BIG_INVENTORY_SPARE (weapons, shields and
 * blades in save space the game never uses, the rest in the container). The container keeps
 * the slots the bag doesn't use. */
#define VS_BIG_INVENTORY_CONTAINER 1
#define VS_BIG_INVENTORY_SPARE 2
#ifdef VS_BIG_INVENTORY
#define VS_CONTAINER_BAG_GRIPS 16
#define VS_CONTAINER_BAG_ARMOR 16
#define VS_CONTAINER_BAG_GEMS 48
#if VS_BIG_INVENTORY == VS_BIG_INVENTORY_SPARE
#define VS_CONTAINER_BAG_WEAPONS 0
#define VS_CONTAINER_BAG_SHIELDS 0
#define VS_CONTAINER_BAG_BLADES 0
#else
#define VS_CONTAINER_BAG_WEAPONS 8
#define VS_CONTAINER_BAG_SHIELDS 8
#define VS_CONTAINER_BAG_BLADES 16
#endif
#else
#define VS_CONTAINER_BAG_WEAPONS 0
#define VS_CONTAINER_BAG_SHIELDS 0
#define VS_CONTAINER_BAG_BLADES 0
#define VS_CONTAINER_BAG_GRIPS 0
#define VS_CONTAINER_BAG_ARMOR 0
#define VS_CONTAINER_BAG_GEMS 0
#endif
/* How many items the container may hold per category */
#define VS_CONTAINER_WEAPONS (32 - VS_CONTAINER_BAG_WEAPONS)
#define VS_CONTAINER_SHIELDS (32 - VS_CONTAINER_BAG_SHIELDS)
#define VS_CONTAINER_BLADES (64 - VS_CONTAINER_BAG_BLADES)
#define VS_CONTAINER_GRIPS (64 - VS_CONTAINER_BAG_GRIPS)
#define VS_CONTAINER_ARMOR (64 - VS_CONTAINER_BAG_ARMOR)
#define VS_CONTAINER_GEMS (192 - VS_CONTAINER_BAG_GEMS)

/* The longest list the item menus show (misc, or the gems when doubled) */
#define VS_INVENTORY_MAX_ROWS (VS_INVENTORY_GEMS > VS_INVENTORY_MISC ? VS_INVENTORY_GEMS : VS_INVENTORY_MISC)
/* sizeof(vs_main_inventory_t) */
#define VS_INVENTORY_BYTES                                                                       \
    (VS_INVENTORY_WEAPONS * 0x20 + VS_INVENTORY_SHIELDS * 0x30 + VS_INVENTORY_BLADES * 0x2C       \
        + VS_INVENTORY_GRIPS * 0x10 + VS_INVENTORY_ARMOR * 0x28 + VS_INVENTORY_GEMS * 0x1C        \
        + VS_INVENTORY_MISC * 4)

/* The order the bag's items are listed in: 1-based slots, 0 ends a list. */
typedef struct {
    u_char weapons[8];
    u_char blades[16];
    u_char grips[16];
    u_char shields[8];
    u_char armor[16];
    u_char gems[48];
    u_char misc[64];
} vs_main_savedInventoryIndices_t;

#ifdef VS_BIG_INVENTORY
typedef struct {
    u_char weapons[VS_INVENTORY_WEAPONS];
    u_char blades[VS_INVENTORY_BLADES];
    u_char grips[VS_INVENTORY_GRIPS];
    u_char shields[VS_INVENTORY_SHIELDS];
    u_char armor[VS_INVENTORY_ARMOR];
    u_char gems[VS_INVENTORY_GEMS];
    u_char misc[VS_INVENTORY_MISC];
} vs_main_inventoryIndices_t;
#else
typedef vs_main_savedInventoryIndices_t vs_main_inventoryIndices_t;
#endif

typedef struct {
#ifdef VS_BIG_INVENTORY
    char unk0[VS_INVENTORY_BYTES]; /* the bag while the container is open */
#else
    char unk0[15][256];
#endif
    vs_main_inventoryIndices_t unkF00;
    vs_menu_containerData unkFB0;
    vs_menu_containerData unk4BB0;
    vs_menu_containerData unk87B0;
    u_short itemsToTransfer[64];
    vs_menu_container unkC430;
    vs_menu_container unk105B0;
} vs_menu_inventoryStorage_t;

enum vs_itemCategories {
    itemCategoryWeapon,
    itemCategoryBlade,
    itemCategoryGrip,
    itemCategoryShield,
    itemCategoryArmor,
    itemCategoryGem,
    itemCategoryMisc,
    itemCategoryManage
};

enum vs_gemTargets { gemTargetShield = 0x80 };

typedef struct {
    char difficultyFlags;
    char unk1;
    char unk2;
    char unk3;
    vs_main_inventoryBlade blade;
    vs_main_inventoryGrip grip;
    vs_main_inventoryGem gems[3];
    char name[24];
} vs_battle_lootedWeapon;

typedef struct {
    char difficultyFlags;
    char unk1;
    char unk2;
    char unk3;
    vs_main_inventoryBlade blade;
} vs_battle_lootedBlade;

typedef struct {
    char difficultyFlags;
    char unk1;
    char unk2;
    char unk3;
    vs_main_inventoryGrip grip;
} vs_battle_lootedGrip;

typedef struct {
    char difficultyFlags;
    char unk1;
    char unk2;
    char unk3;
    vs_main_inventoryArmor shield;
    vs_main_inventoryGem gems[3];
} vs_battle_lootedShield;

typedef struct {
    char difficultyFlags;
    char unk1;
    char unk2;
    char unk3;
    vs_main_inventoryArmor armor;
} vs_battle_lootedArmor;

typedef struct {
    char difficultyFlags;
    char unk1;
    char unk2;
    char unk3;
    vs_main_inventoryArmor accessory;
} vs_battle_lootedAccessory;

typedef struct {
    char difficultyFlags;
    char unk1;
    char unk2;
    char unk3;
    vs_main_inventoryGem gem;
} vs_battle_lootedGem;

typedef struct {
    u_short id;
    char difficultyFlags;
    char count;
} vs_battle_lootedMisc;

typedef struct {
    vs_battle_lootedWeapon weapon;
    vs_battle_lootedBlade blade;
    vs_battle_lootedGrip grip;
    vs_battle_lootedShield shield; // 0xF0
    vs_battle_lootedArmor armor[2]; // 0x170
    vs_battle_lootedAccessory accessory; // 0x1C8
    vs_battle_lootedGem gem; // 0x1F4
    vs_battle_lootedMisc misc[3]; // 0x214
} vs_battle_loot;