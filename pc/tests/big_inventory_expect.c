/* The bag a bigger-bag game should have after loading a save: runs vs_inventory_load
 * (src/BATTLE/BATTLE.PRG/mod/inventory.h) on a decoded save (tools/mod/vs_save.py decode).
 *
 *     big_inventory_expect SAVE.bin OUT.bin
 *
 * SAVE.bin: a decoded savedata_t (0x5C00 bytes). OUT.bin: vs_main_bigInventory followed by
 * vs_main_bigInventoryIndices, as the game holds them at 0x80062000 / 0x80063D00. Built like
 * big_inventory_probe.c. */
#include "common.h"
#include "src/SLUS_010.40/main.h"
#include "src/BATTLE/BATTLE.PRG/mod/inventory.h"

typedef struct FILE FILE;
FILE* fopen(const char*, const char*);
unsigned long fread(void*, unsigned long, unsigned long, FILE*);
unsigned long fwrite(const void*, unsigned long, unsigned long, FILE*);
int fclose(FILE*);
int printf(const char*, ...);

static u_char save[0x5C00];
static vs_main_inventory_t bag;
static vs_main_inventoryIndices_t order;

int main(int argc, char** argv)
{
    FILE* f;
    if (argc != 3)
        return 2;
    f = fopen(argv[1], "rb");
    if (!f || fread(save, 1, sizeof save, f) != sizeof save)
        return 1;
    fclose(f);
    vs_inventory_load(&bag, &order, (vs_main_savedInventory_t*)(save + 0x7C8),
        (vs_main_savedInventoryIndices_t*)(save + 0x16C8), (vs_menu_containerData*)(save + 0x1DE0));
    f = fopen(argv[2], "wb");
    if (!f)
        return 1;
    fwrite(&bag, 1, sizeof bag, f);
    fwrite(&order, 1, sizeof order, f);
    fclose(f);
    printf("%d + %d bytes\n", (int)sizeof bag, (int)sizeof order);
    return 0;
}
