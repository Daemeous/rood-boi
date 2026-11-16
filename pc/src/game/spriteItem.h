/* A sprite as a draw item (pc/src/draw_items.h), what vs_battle_setSprite and its kin made as packets: placed in the
 * ordering table at the slot `ot` (NULL: vs_scratch.unk8, as vs_battle_setSpriteDefault linked) (a marker, as the packet linked there), `brightness` the grey it is drawn in (bit 7 of it
 * set again as bit 8: semi-transparent, as the packet's code took it), its place and size (x low, y high; w low, h high), the
 * draw mode as a GP0 E1 word (vs_getTpage; VS_SPRITE_PAGE: the default texture page; 0: the GPU's) and the texels and CLUT
 * (u, v, CLUT words as vs_getUV0Clut packs them). Returns the item, whose command word ui_text keys on
 * (vs_battle_spriteCommand). Defined in pc/src/battle_helpers.c. */
#ifndef VS_SPRITE_ITEM_H
#define VS_SPRITE_ITEM_H
#define VS_SPRITE_PAGE 0xE100000C
int vs_battle_spriteItem(unsigned* ot, int brightness, int xy, int wh, int mode, int uv_clut);
void vs_battle_tileFlatItem(unsigned* ot, int semi, int rgb, int xy, int wh); /* a flat tile (wh its size), colour 0xBBGGRR */
void vs_battle_shadedItem(unsigned* ot, int verts, int semi, const int* rgb, const int* xy); /* a shaded triangle or quad */
void vs_battle_modeItem(unsigned* ot, int e1); /* the draw mode (GP0 E1) for what draws after it */
void vs_battle_areaItem(unsigned* ot, int x, int y, int w, int h, int offset, int ox, int oy); /* the drawing area (and its offset) */
void vs_battle_maskItem(unsigned* ot, int on); /* the mask bit set when drawing (GP0 E6) */
int vs_battle_gt4PlainItem(unsigned* ot, int semi, int tpage, int clut, const int* rgb, const int* xy, const int* uv); /* a textured Gouraud quad: colours 0xBBGGRR, places (x low, y high) and texels (u low, v high) per corner; returns the item */
void vs_battle_itemBefore(int item, int e1); /* the textured polygon draws after the draw mode e1 (GP0 E1: its dither and display bits) */
void vs_battle_itemAfter(int item, int e1); /* the draw mode e1 (GP0 E1) stays set after the item */
const void* vs_battle_spriteCommand(int item);
int vs_battle_spriteColor(int item, int rgb, int semi); /* its colour 0xBBGGRR and whether it is semi-transparent; returns the item */
#endif
