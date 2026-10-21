/* Widescreen (16:9 Hor+) hooks for the game code, under ROOD_PC only.
 *
 * The GTE narrows projected X by 3/4 about the screen centre (pc_gte_widescreen) and the
 * hardware rasterizer compresses 2D the same way about the screen's centre
 * (pc/src/gpu_hw.c). 2D the game places at a projected point (damage
 * numbers, labels and icons over characters, the map screen's icons) would
 * be narrowed twice; those projections are taken un-narrowed instead
 * (PC_GTE_UNNARROWED), or converted back (pc_gte_unnarrow_x) where the same
 * projection also draws 3D. Screen furniture that hugs a screen edge is
 * anchored to that edge of the 16:9 view (pc_hw_anchor_packets: the packets
 * allocated between start and end; PC_ANCHOR_*). */
#ifndef ROOD_WIDESCREEN_H
#define ROOD_WIDESCREEN_H
#ifdef ROOD_PC
extern int pc_gte_anchored; /* > 0: projections are not narrowed */
int pc_gte_unnarrow_x(int sx); /* a narrowed screen X as the 4:3 projection gives it */
void pc_hw_anchor_packets(const void* start, const void* end, int anchor) __attribute__((weak));
/* What the next LoadImage is: PC_UPLOAD_SCREEN_IMAGE, 2D loaded into the
 * shown frame buffer (drawn compressed); PC_UPLOAD_SCREEN_PICTURE, a picture
 * loaded into a frame buffer before it is shown (compressed once shown). */
void pc_hw_upload_mode(int mode) __attribute__((weak));
enum { PC_UPLOAD_SCREEN_IMAGE = 1, PC_UPLOAD_SCREEN_PICTURE = 2 };
#define PC_UPLOAD_AS(mode) \
    do { \
        if (pc_hw_upload_mode) \
            pc_hw_upload_mode(mode); \
    } while (0)
/* PC_ANCHOR_FILL: a full-screen picture magnified to fill the 16:9 view.
 * (0x10: part of the layout fixes, off with ROOD_WIDESCREEN_LAYOUT=0.) */
/* PC_ANCHOR_SCENE: 2D that is part of the 3D view (the sky panorama, weather
 * particles at projected points, the background sphere that is the field's
 * clear, pc/src/boot_sdk.c): compressed like the rest of the 2D but
 * clipped to the whole 16:9 view, so what the game draws past the 4:3
 * screen's sides shows there. PC_ANCHOR_COVER: screen-space 2D spread over
 * the whole screen (dust motes), drawn over the whole view. */
/* PC_ANCHOR_PROJECTED: 2D the game computes from (narrowed) GTE projections
 * (the area-targeting floor fill's scanline spans, pc/src/map_view.c): drawn
 * where it is, like 3D, never compressed. Without it only the projection log
 * tells (boot_runtime.c narrowed_2d), which matches a span's ends only where
 * they happen to land on a projected point: the rest were compressed. */
enum { PC_ANCHOR_CENTRE = 0x10, PC_ANCHOR_LEFT_EDGE = 0x11, PC_ANCHOR_RIGHT_EDGE = 0x12, PC_ANCHOR_FILL = 0x13,
    PC_ANCHOR_SCENE = 0x14, PC_ANCHOR_COVER = 0x15, PC_ANCHOR_PROJECTED = 0x16 };
/* PC_ANCHOR_EXACT, or'ed with one of the above: a range placed by data (the
 * classic look drawn in Lua, pc/src/classic): scaled about its anchor and
 * nothing more: no edge extension, no cover or backdrop guesses, no side
 * paint (gpu_hw.c's heuristics are for the game's own 2D). */
#define PC_ANCHOR_EXACT 0x20
/* The 4:3 screen's X range widened to the 16:9 view, for culling tests on
 * un-narrowed screen X (PC_GTE_UNNARROWED projections): 320 * (4/3 - 1) / 2
 * = 53.3 more pixels each side in widescreen, 0 otherwise. */
extern int pc_gte_widescreen;
#define PC_WIDESCREEN_MARGIN (pc_gte_widescreen ? 54 : 0)
#define PC_GTE_UNNARROWED(statement) \
    do { \
        ++pc_gte_anchored; \
        statement; \
        --pc_gte_anchored; \
    } while (0)
/* Anchor the packets the statement allocates from the scratchpad's packet
 * pointer (0x1F800000). */
#define PC_ANCHOR_PACKETS(anchor, statement) \
    do { \
        void* pc_anchor_start = psx_load(void*, (u_int*)0x1F800000); \
        statement; \
        if (pc_hw_anchor_packets) \
            pc_hw_anchor_packets(pc_anchor_start, psx_load(void*, (u_int*)0x1F800000), anchor); \
    } while (0)
#endif
#endif
