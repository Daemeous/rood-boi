/* GIM pictures drawn as real text (gitea #14; gim_text.cpp): the game side. vs_render_drawGimTiles and vs_render_drawGimScanlineWipe (game/battle, ROOD_PC) ask here
 * before they draw an image: the answer is yes when the player's text pack has the words of this picture and the text setting is the modern fonts, and then they
 * leave out what the pack's boxes cover (pc_gim_text_boxes) and ask for the carriers of its lines (pc_gim_text_words) where they draw it.
 * Compiled with the game headers (battle builds): the identity of the image is the loader's record (vs_render_gimLayers, its id: the game's GIM table
 * index, and its place among the three records: which of the file's images it is). */
#include "common.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"

extern gim_t* vs_render_gimLayers;
int pc_gim_text_query(int id, int layer);                                                 /* gim_text.cpp */
void pc_gim_text_carry(int id, int layer, int columns, int rows, int scale, int ox, int oy, int brightness, unsigned* ot, const short* left, const short* right);

static int identity(gim_t* image, int* layer)
{
    if (!image || !vs_render_gimLayers)
        return 0;
    *layer = (int)(image - vs_render_gimLayers);
    return *layer >= 0 && *layer <= 2;
}

/* 1: the picture's words are drawn as text this frame (what its pack's boxes cover is left out of the picture). */
int pc_gim_text_replace(gim_t* image)
{
    int layer;
    return identity(image, &layer) ? pc_gim_text_query(image->id, layer) : 0;
}

/* The picture's lines as carriers at `ot`: `scale` the image's (4096 = 1:1), ox and oy its offsets, `brightness` its (0x80 full; bit 8 semi-transparent); a scanline
 * wipe passes its spans (the 320-wide picture's visible left and right per line, 240 lines). */
void pc_gim_text_words(gim_t* image, int columns, int rows, int scale, int ox, int oy, int brightness, unsigned* ot, const short* left, const short* right)
{
    int layer;
    if (identity(image, &layer))
        pc_gim_text_carry(image->id, layer, columns, rows, scale, ox, oy, brightness, ot, left, right);
}
