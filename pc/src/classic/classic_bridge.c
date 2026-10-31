/* The classic layer's game side (battle builds, compiled with the game
 * headers): src's PC_CLASSIC hook (classic_hook.h). The HUD kinds (status,
 * chain, buff, damage) go to the HUD layer (hud_bridge.c pc_hud_element,
 * which captures them and runs their classic claims and the parity gate);
 * so do the gauges (pc_hud_gauges: a HUD kind of the classic look only, inside the panel); the
 * sites only the classic layer has are captured here (RPC packets):
 *   status_panel  func_800CA2DC (146C.c's frame): HP / MP / RISK gauges and
 *                 digits, RISK ZERO, the limbs' figure, the status icons, the
 *                 chain abilities' icons, the top-right sprite; its slots:
 *                 vs_scratch.unk8 (the ability icons) and unk4 - 4 .. - 16. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "classic_parity.h"
#include "classic_hook.h"
/* (the hook's site numbers are the classic layer's kinds) */
typedef char pc_classic_sites_agree[PC_CLASSIC_SITE_GAUGES == PC_CLASSIC_GAUGES
            && PC_CLASSIC_SITE_STATUS_PANEL == PC_CLASSIC_STATUS_PANEL
            && PC_CLASSIC_SITE_FIGURE == PC_CLASSIC_FIGURE
            && PC_CLASSIC_SITE_STATUS_ICONS == PC_CLASSIC_STATUS_ICONS
            && PC_CLASSIC_SITE_BATTLE_MODE == PC_CLASSIC_BATTLE_MODE
            && PC_CLASSIC_SITE_ABILITY_ICONS == PC_CLASSIC_ABILITY_ICONS
            && PC_CLASSIC_SITE_ACTOR_WORD == PC_CLASSIC_ACTOR_WORD && PC_CLASSIC_SITE_TIMER == PC_CLASSIC_TIMER
            && PC_CLASSIC_SITE_EMOTE == PC_CLASSIC_EMOTE && PC_CLASSIC_SITE_TEXT_FRAME == PC_CLASSIC_TEXT_FRAME
            && PC_CLASSIC_SITE_TEXT_BAND == PC_CLASSIC_TEXT_BAND && PC_CLASSIC_SITE_BOX_TEXT == PC_CLASSIC_BOX_TEXT
            && PC_CLASSIC_SITE_IMAGE == PC_CLASSIC_IMAGE && PC_CLASSIC_SITE_TARGET_ROW == PC_CLASSIC_TARGET_ROW
            && PC_CLASSIC_SITE_TARGET_LINE == PC_CLASSIC_TARGET_LINE
            && PC_CLASSIC_SITE_TARGET_MARKER == PC_CLASSIC_TARGET_MARKER
            && PC_CLASSIC_SITE_MENU_BACKGROUND == PC_CLASSIC_MENU_BACKGROUND
            && PC_CLASSIC_SITE_MENU_COMMAND == PC_CLASSIC_MENU_COMMAND && PC_CLASSIC_SITE_MENU_ROW == PC_CLASSIC_MENU_ROW
            && PC_CLASSIC_SITE_MENU_CURSOR == PC_CLASSIC_MENU_CURSOR && PC_CLASSIC_SITE_MENU_TABS == PC_CLASSIC_MENU_TABS
            && PC_CLASSIC_SITE_OPTION_VALUE == PC_CLASSIC_OPTION_VALUE
            && PC_CLASSIC_SITE_ITEMS_CATEGORY == PC_CLASSIC_ITEMS_CATEGORY
            && PC_CLASSIC_SITE_ITEMS_STATUS_BUTTON == PC_CLASSIC_ITEMS_STATUS_BUTTON
            && PC_CLASSIC_SITE_EQUIP_STATS == PC_CLASSIC_EQUIP_STATS && PC_CLASSIC_SITE_DP_PP_BARS == PC_CLASSIC_DP_PP_BARS
            && PC_CLASSIC_SITE_GEM_SLOTS == PC_CLASSIC_GEM_SLOTS && PC_CLASSIC_SITE_STAT_PAGE == PC_CLASSIC_STAT_PAGE
            && PC_CLASSIC_SITE_OK_MARK == PC_CLASSIC_OK_MARK && PC_CLASSIC_SITE_STORAGE_BUTTONS == PC_CLASSIC_STORAGE_BUTTONS
            && PC_CLASSIC_SITE_STORAGE_COUNTS == PC_CLASSIC_STORAGE_COUNTS
            && PC_CLASSIC_SITE_LOOT_CAPACITIES == PC_CLASSIC_LOOT_CAPACITIES
            && PC_CLASSIC_SITE_DISCARD_COUNT == PC_CLASSIC_DISCARD_COUNT
            && PC_CLASSIC_SITE_RENAME_GRID == PC_CLASSIC_RENAME_GRID && PC_CLASSIC_SITE_RENAME_BOX == PC_CLASSIC_RENAME_BOX
            && PC_CLASSIC_SITE_FILE_MENU == PC_CLASSIC_FILE_MENU && PC_CLASSIC_SITE_FILE_ELEMENT == PC_CLASSIC_FILE_ELEMENT
        ? 1
        : -1];

void pc_hud_element(int kind, void* record, int x, int y, void* first, void* end);
void pc_classic_site(int kind, void* record, int x, int y, void* first, void* end);
int pc_classic_walk_slots(const int* offsets, int count, int with_unk8, int enclosing, const void* first,
    const void* end, pc_classic_packet* out, int max);
void pc_hud_figure(void* record, int stance, void* first, void* end);
void pc_hud_status_icons(void* record, void* first, void* end);
void pc_hud_battle_mode(void* record, void* first, void* end);
void pc_hud_ability_icons(void* record, void* first, void* end);
void pc_hud_actor_word(void* record, int x, int y, void* first, void* end);
void pc_hud_timer(void* record, int x, int y, void* first, void* end);
void pc_hud_emote(void* record, int actor, void* first, void* end);
void pc_hud_text_frame(void* record, int id, void* first, void* end);
void pc_hud_text_band(void* record, int id, void* first, void* end);
void pc_hud_box_text(void* record, int id, void* first, void* end);
void pc_hud_image(void* record, void* first, void* end);
void pc_hud_target_row(void* record, void* first, void* end);
void pc_hud_target_line(void* record, void* first, void* end);
void pc_hud_target_marker(void* record, void* first, void* end);
void pc_hud_gauges(void* record, void* first, void* end); /* hud_bridge.c */
void pc_hud_menu(int kind, void* record, void* first, void* end); /* hud_bridge.c: MAINMENU's frame */
extern unsigned pc_vsync_count(void) __attribute__((weak));

/* A packet's anchor: the range it lies in, else the one its caller gives the site's range after the
 * hook returns (enclosing: the gauges inside the panel, anchored by 146C.c once func_800CA2DC is done). */
static int enclosed_anchor(const unsigned* tag, void* user)
{
    int anchor = pc_classic_guest().anchor(tag, NULL);
    return anchor ? anchor : *(const int*)user;
}

/* Walks slots (byte offsets from vs_scratch.unk4, or the unk8 one) in drawing order (the
 * ordering table is drawn from its top: the higher slot first), into out. */
int pc_classic_walk_slots(const int* offsets, int count, int with_unk8, int enclosing, const void* first,
    const void* end, pc_classic_packet* out, int max)
{
    pc_classic_walker walker = pc_classic_guest();
    const u_long* slots[8];
    int n = 0, total = 0, i, j;
    char* base = psx_get(vs_scratch.unk4);
    walker.anchor = enclosed_anchor;
    walker.user = &enclosing;
    for (i = 0; i < count && n < 7; ++i)
        slots[n++] = (const u_long*)(base + offsets[i]);
    if (with_unk8)
        slots[n++] = (const u_long*)psx_get(vs_scratch.unk8);
    for (i = 1; i < n; ++i) /* (insertion sort: highest address first) */
        for (j = i; j > 0 && slots[j] > slots[j - 1]; --j) {
            const u_long* t = slots[j];
            slots[j] = slots[j - 1];
            slots[j - 1] = t;
        }
    for (i = 0; i < n; ++i) {
        if (i && slots[i] == slots[i - 1])
            continue;
        total += pc_classic_walk(&walker, (const unsigned*)slots[i], first, end, out + total, max - total);
    }
    return total;
}

static const int panel_slots[] = { -4, -8, -12, -16 };

/* photo.cpp (rood.ui.hide{hud}): the HUD hidden; every site's packets become no-ops after it ran (its
 * element recorded, its state stepped, as the game does). */
extern int pc_photo_hide_hud(void) __attribute__((weak));
extern int pc_photo_hud_words __attribute__((weak)), pc_photo_hud_timer __attribute__((weak));

void pc_classic_site(int kind, void* record, int x, int y, void* first, void* end)
{
    static pc_classic_packet captured[PC_CLASSIC_CAPTURE];
    /* (the HUD's sites, the words over the actors and the timer with them (photo_ui.cpp hud_words, hud_timer);
     * the emotes, the text boxes and the images were never the HUD's to hide; the dialogue has its own, GAME2D) */
    int hide = pc_photo_hide_hud && pc_photo_hide_hud()
        && (kind != PC_CLASSIC_ACTOR_WORD || (&pc_photo_hud_words && pc_photo_hud_words))
        && (kind != PC_CLASSIC_TIMER || (&pc_photo_hud_timer && pc_photo_hud_timer))
        && kind != PC_CLASSIC_EMOTE && kind != PC_CLASSIC_TEXT_FRAME && kind != PC_CLASSIC_TEXT_BAND
        && kind != PC_CLASSIC_BOX_TEXT && kind != PC_CLASSIC_IMAGE;
    if (kind >= PC_CLASSIC_MENU_BACKGROUND && kind <= PC_CLASSIC_RENAME_BOX) { /* (the menus: never the HUD's to hide) */
        pc_hud_menu(kind, record, first, end);
        return;
    }
    if (kind >= PC_CLASSIC_STATUS && kind <= PC_CLASSIC_DAMAGE) {
        pc_hud_element(kind, record, x, y, first, end);
        if (hide) /* (a classic claim's own packets too: allocated after the game's) */
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_TARGET_ROW || kind == PC_CLASSIC_TARGET_LINE || kind == PC_CLASSIC_TARGET_MARKER) {
        /* (targeting is the HUD's: hidden with it) */
        if (kind == PC_CLASSIC_TARGET_ROW)
            pc_hud_target_row(record, first, end);
        else if (kind == PC_CLASSIC_TARGET_LINE)
            pc_hud_target_line(record, first, end);
        else
            pc_hud_target_marker(record, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_IMAGE) {
        pc_hud_image(record, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_BOX_TEXT) {
        pc_hud_box_text(record, x, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_TEXT_BAND) {
        pc_hud_text_band(record, x, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_TEXT_FRAME) {
        pc_hud_text_frame(record, x, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_EMOTE) {
        pc_hud_emote(record, x, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_TIMER) {
        pc_hud_timer(record, x, y, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_ACTOR_WORD) {
        pc_hud_actor_word(record, x, y, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_ABILITY_ICONS) {
        pc_hud_ability_icons(record, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_BATTLE_MODE) {
        pc_hud_battle_mode(record, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_STATUS_ICONS) {
        pc_hud_status_icons(record, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_FIGURE) {
        pc_hud_figure(record, x, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_GAUGES) {
        pc_hud_gauges(record, first, end);
        if (hide)
            pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (hide && kind == PC_CLASSIC_STATUS_PANEL) {
        pc_classic_suppress(first, psx_get(vs_scratch.unk0));
        return;
    }
    if (kind == PC_CLASSIC_STATUS_PANEL && pc_classic_capturing()) {
        int n = pc_classic_walk_slots(panel_slots, 4, 1, 0, first, end, captured, PC_CLASSIC_CAPTURE);
        static pc_classic_packet scanned[PC_CLASSIC_CAPTURE];
        pc_classic_capture_game(kind, captured, n, pc_vsync_count ? pc_vsync_count() : 0, "");
        pc_classic_walker guest = pc_classic_guest();
        pc_classic_capture_allocated(kind, pc_classic_scan(&guest, first, end, scanned, PC_CLASSIC_CAPTURE));
    }
}
