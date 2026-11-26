/* The host-only link (rood-studio as its own executable, no game units): what the host's units still name from the game, answered here.
 * Nothing of the game runs in this executable: its main hands straight to rood_studio.c. These are the 'no game' answers:
 *   - the room the studio shows (the same words the game's studio holds for it, studio.c pc_studio_app_room) for the scene capture and ray tracing;
 *   - a vsync is the host's tick (the frame is presented); the game's functions that the host's units reach but the studio never calls do nothing;
 *   - the few game tables the host reads are empty (a game that is not loaded: zeros, as the game's own are before it loads anything). */
#include <stdint.h>
#include <string.h>

extern void pc_tick(void);

/* ---- the room ---- */
static unsigned char* room_block;
static unsigned room_length;
void pc_studio_app_room(unsigned char* block, unsigned length)
{
    room_block = block;
    room_length = length;
}
int pc_room_loaded(void) { return room_block != 0; }
const unsigned char* pc_room_geometry(unsigned* length)
{
    *length = room_length;
    return room_block;
}
const unsigned char* pc_room_effects(unsigned* length)
{
    *length = 0;
    return 0;
}
const unsigned* pc_room_light_table(int* enabled)
{
    *enabled = room_block != 0;
    return 0;
}

/* ---- the vsync ---- */
int VSync(int mode)
{
    if (mode >= 0)
        pc_tick();
    return 0;
}
void pc_vblank_wait(void) { pc_tick(); }

/* ---- game functions the host's units reach and the studio does not call ---- */
void* RotMatrixYXZ_gte(void* vector, void* matrix) { (void)vector; return matrix; }
void pc_deliver_event(unsigned long event, long spec) { (void)event; (void)spec; }
void pc_sound_lfo_quirk_load(const unsigned char* bytes) { (void)bytes; }
void vs_main_exec(void) {}
int pc_weather_raining(void) { return 0; }
void pc_ui_ashley_backdrop_built(int* kept, int* need) { *kept = *need = 0; }
int vs_battle_ft4Item(unsigned* ot, int raw, int semi, int tpage, int clut, int rgb, const int* xy, const int* uv)
{
    (void)ot, (void)raw, (void)semi, (void)tpage, (void)clut, (void)rgb, (void)xy, (void)uv;
    return 0;
}
void vs_battle_itemBracket(int item) { (void)item; }
unsigned* vs_battle_itemCursor(void)
{
    static unsigned cursor[2];
    return cursor;
}
const void* vs_battle_spriteCommand(int item) { (void)item; return 0; }
int vs_battle_spriteItem(unsigned* ot, int brightness, int xy, int wh, int mode, int uv_clut)
{
    (void)ot, (void)brightness, (void)xy, (void)wh, (void)mode, (void)uv_clut;
    return 0;
}

/* ---- game data the host reads ---- */
const uint32_t rood_game_abi = 1;
uint16_t pc_spu_regs[0x100];
const int pc_overlay_statics_count;
const char pc_overlay_statics[1];
static unsigned char overlay_zero[0x90000]; /* BATTLE.PRG's slot: nothing loaded */
void* const vs_overlay_slots[5] = {overlay_zero};
void* vs_scratch[4];
unsigned char vs_view[0x58];
unsigned char vs_xform[0xA4];
