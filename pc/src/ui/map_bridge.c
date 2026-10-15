/* Modern menus, the Map frame: the game side (battle builds; map_frame.h).
 *
 * The shell's Map row (Leá Monde) delivers command 5 as before: MAINMENU
 * runs MENU5 (vs_menu5_exec) and MENU5 stays the only state machine: its pad,
 * rendering, sounds, VRAM, HUD, world freeze, CD reads and exit codes are
 * the original's. With ui_menus modern and the overlay up the frame differs
 * in three things only (4D8.c, ROOD_PC):
 *   - Circle's area list is this file's (the list below, MAINMENU 58EC.c's
 *     rows list ported), run inside MENU5's own displayMapMenu state, which
 *     still does the switch itself (816-826);
 *   - MENU5's controls help, compass and JAMMING (_drawUIControls) are not
 *     drawn: the shell draws its own (ui_map_draw.cpp);
 *   - the pointer's requests (ui_map.cpp's mailbox) are applied by MENU5 at
 *     its input point (processInput) with its own statements and clamps.
 *
 * Lifecycle (map_frame.h's model):
 *   off      nothing armed or framed
 *   armed    menu_bridge.c deliver(6): the shell hid for the map; the frame
 *            draws "Loading map" until MENU5's first stamp (30 vsyncs at most)
 *   framed   vs_menu5_exec state 3 latched this run (pc_ui_map_frame_begin);
 *            the chrome draws while its stamp is at most 8 vsyncs old, so a
 *            stalled MENU5 never leaves chrome over the live world
 *   end      Cross: MENU5 returns 1, func_800FAA5C reopens the top level,
 *            pc_ui_menu_open calls pc_ui_map_end (a pending tab from a tab
 *            click goes back to the shell); Triangle: currentMenu 6 then
 *            0x7F, pc_ui_menu_heartbeat ends it; a soft reset forgets it.
 *
 * The frame writes no game RAM itself but what MENU5's list writes: the
 * cursor memories (D_800F4EE8.cursorMemories[0..1]) on open (4D8.c
 * _pcListOpen, _configureMapMenu's formula) and on exit (58EC.c case 17:
 * row in the window, scroll). The completion figure is
 * vs_battle_getMapCompletion's on a copy of the room flags (its fix-up of
 * roomFlags[1] is not written). */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "ui_shell.h"
#include "map_frame.h"
#include "map_pad.h"
#include <libetc.h>

extern u_short _mapNames[]; /* MENU5 4D8.c: the area names (index = scene id) */
extern unsigned pc_vsync_count(void) __attribute__((weak));
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */
unsigned pc_ui_map_buttons(unsigned pad); /* menu_bridge.c: libetc PAD bits to PC_UI_* */
int pc_ui_shell_step_tab(int dir);        /* ui_shell.cpp */

extern vs_main_HeapHeader heapA; /* main.c: the game heap's free list (vs_main_allocHeap) */

/* The game heap's largest free block in bytes (walking the free list as
 * vs_main_allocHeap does; ashley_bridge.c heap_largest): ui_menu's
 * "map_heap_free", so a test sees the map screen give back what it took. */
int pc_ui_map_heap_free(void)
{
    vs_main_HeapHeader* h = psx_get(heapA.next);
    u_int best = 0;
    int guard;
    for (guard = 0; h && h != &heapA && guard < 0x10000; ++guard) {
        if (h->blockSz > best)
            best = h->blockSz;
        h = psx_get(h->next);
    }
    return (int)(best * 16);
}

static int pending_tab = -1; /* a tab click (or the list's L1 / R1): the shell reopens there */
static int pick;             /* an area row clicked: the list confirms that scene's row */
static int ready;            /* the list's rows still sliding in: the pad is not read (58EC.c case 16) */

static unsigned now(void) { return pc_vsync_count ? pc_vsync_count() : 0; }

static void zero(void* p, unsigned n)
{
    unsigned char* b = p;
    while (n--)
        *b++ = 0;
}

static void decode(const void* text, int max_bytes, char* out)
{
    out[0] = 0;
    if (text)
        pc_ui_text_decode(text, max_bytes, out, PC_UI_MAP_NAME);
}

static int area_visited(int id)
{
    return id > 0 && id < 32 && (vs_main_mapStatus.areaFlags[id >> 5] & (1 << (id & 0x1F)));
}

/* vs_battle_getCurrentSceneId: 32 or more is no area (4D8.c _loadArmFile). */
static int current_scene(void)
{
    int id = vs_battle_getCurrentSceneId();
    return id >= 32 ? 0 : id;
}

/* vs_battle_getMapCompletion (146C.c:17291) on a copy: the roomFlags[1]
 * fix-up stays out of the game's RAM. */
static int completion(void)
{
    int flags[16];
    int i, j, value = 0;
    for (i = 0; i < 16; ++i)
        flags[i] = vs_main_mapStatus.roomFlags[i];
    if (flags[1] & 0x800000)
        flags[1] |= 0x400000;
    for (i = 0; i < 16; ++i)
        for (j = 0; j < 32; ++j)
            if (flags[i] & vs_battle_mapCompletionFlags[i] & (1 << j))
                ++value;
    return value * 100 / 361;
}

/* The visited areas (bits 1..31 of areaFlags), named as MENU5's list names
 * them (_mapNames[_mapNames[i]]). */
static void fill_areas(pc_ui_map_model* m)
{
    int i;
    m->current_scene = current_scene();
    if (m->framed && m->published && m->scene_row == 0)
        m->current_scene = 0;
    m->area_count = 0;
    for (i = 1; i < 32; ++i) {
        pc_ui_map_area* a;
        if (!area_visited(i))
            continue;
        a = &m->areas[m->area_count++];
        a->id = i;
        a->current = i == m->current_scene;
        a->shown = m->published && i == m->scene;
        a->paling = vs_main_stateFlags.mapPaling[i] != 0;
        decode(&_mapNames[_mapNames[i]], 64, a->name);
    }
    m->completion = completion();
}

/* menu_bridge.c deliver(6): the shell hid for the map. */
void pc_ui_map_arm(void)
{
    pc_ui_map_model* m = pc_ui_map_state();
    zero(m, sizeof *m);
    m->armed = 1;
    m->armed_vsync = now();
    pending_tab = -1;
    pick = ready = 0;
    pc_ui_map_reset_mailbox();
    fill_areas(m);
}

int pc_ui_map_on(void)
{
    pc_ui_map_model* m = pc_ui_map_state();
    return m->armed || m->framed;
}

/* The frame ends (the shell reopens, the menu closed, the game left the
 * map): the pending tab, or -1. */
int pc_ui_map_end(void)
{
    int t = pending_tab;
    pc_ui_map_model* m = pc_ui_map_state();
    if (!m->armed && !m->framed)
        return -1;
    zero(m, sizeof *m);
    pc_ui_map_reset_mailbox();
    pending_tab = -1;
    pick = ready = 0;
    return t;
}

/* A soft reset (menu_bridge.c pc_ui_menu_reset): nothing to free. */
void pc_ui_map_forget(void) { pc_ui_map_end(); }

int pc_ui_map_frame_begin(int run)
{
    pc_ui_map_model* m = pc_ui_map_state();
    if (!m->armed)
        return 0;
    m->armed = 0;
    m->framed = 1;
    m->run = run;
    m->stamped = 1;
    m->stamp = now();
    m->exec_state = 3;
    return 1;
}

void pc_ui_map_frame_stamp(int exec_state)
{
    pc_ui_map_model* m = pc_ui_map_state();
    if (!m->framed || m->finished)
        return;
    m->stamped = 1;
    m->stamp = now();
    m->exec_state = exec_state;
}

void pc_ui_map_frame_finish(int quit_state)
{
    pc_ui_map_model* m = pc_ui_map_state();
    if (!m->framed)
        return;
    m->finished = 1;
    m->quit_state = quit_state;
    m->exec_state = 10;
    m->list.open = 0;
    pc_ui_map_drop();
}

void pc_ui_map_frame_publish(const pc_ui_map_game* g)
{
    pc_ui_map_model* m = pc_ui_map_state();
    const vs_battle_room* rooms = g->rooms;
    const vs_battle_roomName* names = g->room_names;
    int i, n, at;
    if (!m->framed)
        return;
    m->published = 1;
    m->exec_state = g->exec_state;
    m->menu_state = g->menu_state;
    m->scene = g->scene;
    m->scene_row = g->scene_row;
    m->room = g->room;
    m->is_current_scene = g->is_current_scene;
    m->yaw = g->yaw;
    m->pitch = g->pitch;
    m->yaw0 = g->yaw0;
    m->distance = g->distance;
    m->gx = g->gx;
    m->gy = g->gy;
    m->hide_ui = g->hide_ui;
    m->is_paling = g->is_paling;
    m->paled = g->paled;
    m->quit_state = g->quit_state;
    m->cmd_state = g->cmd_state;
    fill_areas(m);
    n = g->room_count < PC_UI_MAP_ROOMS ? g->room_count : PC_UI_MAP_ROOMS;
    if (!rooms || !names)
        n = 0;
    m->room_count = n;
    m->rooms_seen = 0;
    for (i = 0; i < n; ++i) {
        m->rooms[i].visited = rooms[i].visited != 0;
        m->rooms_seen += m->rooms[i].visited;
        decode(names[i].name, 32, m->rooms[i].name);
    }
    /* the visited rooms in L1 / R1's order (_updateRoomIndex: the next ring),
     * from the first visited one */
    m->ring_count = 0;
    for (at = 0; at < n && !m->rooms[at].visited; ++at) {
    }
    for (i = 0; at < n && i < n; ++i) {
        if (m->rooms[at].visited)
            m->ring[m->ring_count++] = at;
        at = names[at].next;
        if (at < 0 || at >= n || at == m->ring[0])
            break;
    }
    decode(g->paling_text ? &g->paling_text[g->paling_text[0]] : 0, 128, m->paling_text);
    /* requests are MENU5's at processInput only (the list takes its own) */
    if (m->menu_state != 1)
        pc_ui_map_drop();
}

int pc_ui_map_frame_take(pc_ui_map_req* out, int discrete_ok)
{
    for (;;) {
        if (!pc_ui_map_take(out, discrete_ok))
            return 0;
        if (out->kind == PC_UI_MAP_REQ_AREA) {
            if (!area_visited(out->a))
                continue;
            pick = out->a;
        } else if (out->kind == PC_UI_MAP_REQ_TAB) {
            pending_tab = out->a;
        } else if (out->kind == PC_UI_MAP_REQ_PAD) {
            continue; /* the list's own */
        }
        return 1;
    }
}

/* ---- The area list: MAINMENU 58EC.c vs_mainmenu_setMenuRows (rowCount, rowInfo
 * 256: sub-menu 0, one title row, nine rows shown) and vs_mainMenu_renderItemsList's
 * case 17, without its rows (the frame draws them). sel is _selectedRow (the
 * row in the window), page _subMenuPage (the scroll). ---- */
enum { SHOWN = 9 }; /* 10 - rowInfo */
static int sel, page;

static void list_up(int count, int pressed)
{
    if (count <= SHOWN) {
        sel = sel == 0 ? count - 1 : sel - 1;
    } else if (page == 0) {
        if (sel == 0) {
            if (pressed) {
                sel = SHOWN - 1;
                page = count - SHOWN; /* rowInfo + count + 246 as a char */
            }
        } else {
            --sel;
        }
    } else if (sel == 1) {
        --page;
    } else {
        --sel;
    }
}

static void list_down(int count, int pressed)
{
    if (count <= SHOWN) {
        sel = sel == count - 1 ? 0 : sel + 1;
    } else if (page == count - SHOWN) {
        if (sel == SHOWN - 1) {
            if (pressed) {
                sel = 0;
                page = 0;
            }
        } else {
            ++sel;
        }
    } else if (sel == SHOWN - 2) {
        ++page;
    } else {
        ++sel;
    }
}

/* Frames MAINMENU's list takes before it reads the pad: one row brought in
 * a frame (58EC.c 969-988), the last one's slide from x 320 to 194
 * (vs_mainMenu_renderItemsList's slide, 58EC.c 1502-1540: the
 * vs_battle_rowAnimationSteps easing), then case 16's vs_mainmenu_ready. */
static int list_ready_frames(int rows)
{
    int x = 320, n = 0, j;
    while (x != 194 && n < 64) {
        for (j = 1; j < 16; ++j)
            if (194 + (u_char)vs_battle_rowAnimationSteps[j] >= x)
                break;
        x = 194 + (u_char)vs_battle_rowAnimationSteps[j - 1];
        ++n;
    }
    return rows + n + 1;
}

static void list_publish(void)
{
    pc_ui_map_model* m = pc_ui_map_state();
    m->list.cursor = sel + page;
    m->list.scroll = page;
}

void pc_ui_map_frame_list_open(const int* scene_ids, int count, int current_row, int cm0, int cm1)
{
    pc_ui_map_model* m = pc_ui_map_state();
    int i = cm0, temp = count - SHOWN, below;
    m->list.open = 1;
    m->list.count = count < PC_UI_MAP_AREAS ? count : PC_UI_MAP_AREAS;
    m->list.current_row = current_row;
    for (page = 0; page < m->list.count; ++page)
        m->list.ids[page] = scene_ids[page];
    /* vs_mainmenu_setMenuRows with the cursor memory on (_configureMapMenu) */
    page = cm1;
    if (temp < 0) {
        i += page;
        page = 0;
        if (i >= count)
            i = 0;
    } else {
        if (temp < page) {
            if (i + page >= count) {
                i = 0;
                page = 0;
            } else {
                i += page - temp;
                page = temp;
            }
        }
        below = page < temp;
        if (page > 0 && i == 0) {
            i = 1;
            page -= 1;
            below = page < temp;
        }
        if (below && i == SHOWN - 1) {
            i -= 1;
            page += 1;
        }
    }
    sel = i;
    ready = list_ready_frames(count < SHOWN ? count : SHOWN);
    list_publish();
    /* an area row clicked: the cursor goes down to it as the pad would
     * (Down presses), and the list confirms it on its first frame */
    if (pick && count > 0) {
        int n, start = sel + page, target = -1;
        for (n = 0; n < count; ++n)
            if (scene_ids[n] == pick)
                target = n;
        for (n = 0; target >= 0 && n < 2 * count && sel + page != target; ++n)
            list_down(count, 1);
        if (sel + page != start)
            vs_battle_playMenuChangeSfx();
        list_publish();
        if (target < 0)
            pick = 0;
    }
}

/* the window and scroll the list leaves (58EC.c 1012 / 1028 / 1046) */
static void list_close(void)
{
    pc_ui_map_model* m = pc_ui_map_state();
    D_800F4EE8.cursorMemories[0] = sel;
    D_800F4EE8.cursorMemories[1] = page;
    m->list.open = 0;
}

int pc_ui_map_frame_list(void)
{
    pc_ui_map_model* m = pc_ui_map_state();
    pc_ui_map_req r;
    unsigned pressed, repeat;
    int count = m->list.count, abs_row = sel + page, tab = -1, confirm = pick != 0;
    pick = 0;
    if (!m->list.open || count == 0) { /* no area: _mapSelectionMenu's -2, nothing written */
        m->list.open = 0;
        return -2;
    }
    pressed = pc_ui_map_buttons(vs_main_buttonsPressed.all);
    repeat = pc_ui_map_buttons(vs_main_buttonRepeat) | pressed;
    if (ready > 0) { /* the rows still come in: the pad waits (case 16), the pointer does not */
        --ready;
        pressed = repeat = 0;
    }
    /* the pointer's: an area row (confirmed), the prompts' buttons, a tab */
    while (pc_ui_map_take(&r, 1)) {
        if (r.kind == PC_UI_MAP_REQ_PAD) {
            pressed |= (unsigned)r.a;
            repeat |= (unsigned)r.a;
        } else if (r.kind == PC_UI_MAP_REQ_AREA) {
            int n, target = -1;
            for (n = 0; n < count; ++n)
                if (m->list.ids[n] == r.a)
                    target = n;
            for (n = 0; target >= 0 && n < 2 * count && sel + page != target; ++n)
                list_down(count, 1);
            confirm = target >= 0;
        } else if (r.kind == PC_UI_MAP_REQ_TAB) {
            tab = r.a;
        }
    }
    if (pressed & (PC_UI_TAB_PREV | PC_UI_TAB_NEXT))
        tab = pc_ui_shell_step_tab(pressed & PC_UI_TAB_NEXT ? 1 : -1);
    if (pressed & PC_UI_EXIT) { /* Triangle */
        vs_battle_playMenuLeaveSfx();
        list_close();
        return -3;
    }
    if ((pressed & PC_UI_CONFIRM) || confirm) { /* Circle (no row is unselectable) */
        vs_battle_playMenuSelectSfx();
        list_close();
        return sel + page;
    }
    if ((pressed & PC_UI_BACK) || tab >= 0) { /* Cross; a tab: MENU5 quits as Cross, the tab kept */
        vs_battle_playMenuLeaveSfx();
        list_close();
        if (tab >= 0) {
            pending_tab = tab;
            return -4;
        }
        return -2;
    }
    if (repeat & PC_UI_UP)
        list_up(count, (pressed & PC_UI_UP) != 0);
    if (repeat & PC_UI_DOWN)
        list_down(count, (pressed & PC_UI_DOWN) != 0);
    /* Left / Right (the frame's own; 58EC.c has none): a window up or down,
     * eight rows (a list that fits: its first or last row), never wrapping */
    if ((repeat & (PC_UI_LEFT | PC_UI_RIGHT)) && count <= SHOWN) {
        sel = repeat & PC_UI_LEFT ? 0 : count - 1;
    } else if (repeat & (PC_UI_LEFT | PC_UI_RIGHT)) {
        int n;
        for (n = 0; n < SHOWN - 1; ++n) {
            if (repeat & PC_UI_LEFT)
                list_up(count, 0);
            else
                list_down(count, 0);
        }
    }
    if (abs_row != sel + page)
        vs_battle_playMenuChangeSfx();
    list_publish();
    return -1;
}

/* ---- The right stick and the triggers (map_pad.h; 4D8.c processInput) ---- */

extern int pc_qol_map_controls, pc_qol_map_invert;           /* project_settings.cpp */
extern float pc_camera_stick_deadzone;                       /* camera_stick.c */
extern int pc_window_stick(float out[2]) __attribute__((weak)); /* gamepad right stick and keys */
extern int pc_rpc_stick(float out[2]) __attribute__((weak));    /* RPC input {right_stick} */
extern float pc_controller_trigger(int which) __attribute__((weak)); /* boot_gamepad.c: L2 0, R2 1 */
extern int pc_rpc_triggers(float out[2]) __attribute__((weak));      /* RPC input {triggers} */

static pc_map_pad_view pad_view;
static unsigned pad_vsync;

int pc_ui_map_pad(int cmd, short* pitch, short* yaw, int* distance, int* gx, int* gy)
{
    pc_map_pad_in in;
    float v[2];
    unsigned now = pc_vsync_count ? pc_vsync_count() : 0;
    int k;

    vs_main_bzero(&in, sizeof in);
    if (pc_window_stick && pc_window_stick(v))
        in.stick[0] += v[0], in.stick[1] += v[1];
    if (pc_rpc_stick && pc_rpc_stick(v))
        in.stick[0] += v[0], in.stick[1] += v[1];
    for (k = 0; k < 2; ++k)
        in.stick[k] = in.stick[k] < -1 ? -1 : in.stick[k] > 1 ? 1 : in.stick[k];
    if (pc_rpc_triggers && pc_rpc_triggers(v))
        in.trigger[0] = v[0], in.trigger[1] = v[1];
    else if (pc_controller_trigger)
        in.trigger[0] = pc_controller_trigger(0), in.trigger[1] = pc_controller_trigger(1);
    in.held = vs_main_buttonsPreviousState;
    in.deadzone = pc_camera_stick_deadzone;
    in.controls = pc_qol_map_controls;
    in.invert = pc_qol_map_invert;
    if (now - pad_vsync > 2) /* a new run (or a pause): no fraction carried over */
        pad_view.frac[0] = pad_view.frac[1] = pad_view.frac[2] = 0;
    pad_vsync = now;
    pad_view.pitch = *pitch, pad_view.yaw = *yaw, pad_view.distance = *distance;
    pad_view.gx = *gx, pad_view.gy = *gy;
    cmd = pc_map_pad_step(&in, &pad_view, cmd);
    *pitch = (short)pad_view.pitch, *yaw = (short)pad_view.yaw, *distance = pad_view.distance;
    *gx = pad_view.gx, *gy = pad_view.gy;
    return cmd;
}
