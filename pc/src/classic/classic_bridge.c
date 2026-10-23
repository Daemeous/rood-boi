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
void pc_hud_gauges(void* record, void* first, void* end); /* hud_bridge.c */
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

void pc_classic_site(int kind, void* record, int x, int y, void* first, void* end)
{
    static pc_classic_packet captured[PC_CLASSIC_CAPTURE];
    int hide = pc_photo_hide_hud && pc_photo_hide_hud();
    if (kind >= PC_CLASSIC_STATUS && kind <= PC_CLASSIC_DAMAGE) {
        pc_hud_element(kind, record, x, y, first, end);
        if (hide) /* (a classic claim's own packets too: allocated after the game's) */
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
