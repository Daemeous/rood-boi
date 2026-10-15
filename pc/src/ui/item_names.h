/* Names of items and materials for the modern menus (item_names.c), UTF-8.
 *
 * The top level has no vs_mainMenu_itemNames (MENU3 and friends load it), so
 * MENU/ITEMNAME.BIN is read from the disc image once into host memory: 512
 * fixed records of 24 bytes in the game's text encoding, indexed like
 * vs_mainMenu_itemNames. Decoded with pc_ui_text_decode; never written
 * anywhere. MENU/ITEMHELP.BIN (vs_mainMenu_itemHelp: descriptions, sort and
 * discard texts) is read the same way. Plain C, no game headers. */
#ifndef ROOD_UI_ITEM_NAMES_H
#define ROOD_UI_ITEM_NAMES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Entry `id` of the table (0..511): a blade's id, a shield's base.id, an
 * armor's id, a gem's id, ... ("" when out of range or the disc cannot be
 * read). The pointer stays valid until exit. */
const char* pc_ui_item_name(int id);
/* A material by the inventory's numbering (1 wood, ...; the table's entry
 * material - 1 + VS_ITEMNAME_BIN_INDEX_wood); "" for 0. */
const char* pc_ui_material_name(int material);
/* Entry `index` of MENU/ITEMHELP.BIN (vs_mainMenu_itemHelp, indexed as
 * build/assets/MENU/ITEMHELP.BIN.h's VS_ITEMHELP_BIN_INDEX_*), read from the
 * disc image once like ITEMNAME.BIN and decoded on first use; "" when out of
 * range or not readable. Valid until exit. */
const char* pc_ui_item_help(int index);
/* How many entries the table has (0: not readable). */
int pc_ui_item_help_count(void);
/* ITEMNAME.BIN's record `id` as the game has it (24 bytes, the game's
 * encoding; vs_mainMenu_itemNames + id), NULL when out of range or not
 * readable. Valid until exit. */
const unsigned char* pc_ui_item_name_bytes(int id);
/* The whole of disc file `path` ("MENU/BLADE.SYD") into a new malloc'd
 * buffer (*out, *size; the caller frees it); 0 when it cannot be read. Read
 * into host memory only, never written anywhere. */
int pc_ui_disc_read(const char* path, unsigned char** out, unsigned* size);

#ifdef __cplusplus
}
#endif
#endif
