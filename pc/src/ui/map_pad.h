/* The map screen's analog controls (MENU5, src/MENU/MENU5.PRG/4D8.c; System >
 * Controls "Map controls", settings qol_map_controls). On PC the game's
 * stick buffer stays centred (the pad is digital: boot_sdk.c PadInfoMode),
 * so MENU5's stick code never runs; the PC left stick reaches it as the
 * D-pad. This step runs after MENU5's own input each processInput frame
 * (not on a paled area, not while leaving) and drives the view from the
 * right stick and the triggers:
 *
 *   Modern (0, default)  right stick X turns (yaw), Y tilts (pitch), speed
 *                        proportional to deflection past the deadzone (64
 *                        units a frame at full); L2 or R2 held alone (no
 *                        D-pad, no stick, no Start, not both) zooms out / in
 *                        (16 a frame at full, analog with a trigger axis);
 *                        everything else as MENU5 has it (left stick and
 *                        D-pad pan, L2 / R2 + D-pad turn / zoom, L1 / R1
 *                        rooms, Start reset)
 *   Original (1)         MENU5's PlayStation stick code as written, on the
 *                        right stick (past half way): L2 + stick turns and
 *                        tilts, R2 + stick Y zooms, the stick alone pans
 *                        (qol_map_invert as for the D-pad); only while no
 *                        D-pad bit is held (the PS1's stick won over the
 *                        D-pad; here the D-pad step has run first)
 *
 * Pure (no game or SDL headers): map_bridge.c feeds it, map_pad_probe tests it. */
#ifndef PC_UI_MAP_PAD_H
#define PC_UI_MAP_PAD_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_MAP_CONTROLS_MODERN, PC_MAP_CONTROLS_ORIGINAL };

typedef struct {
    float stick[2];   /* right stick -1..1 (SDL: +y down) */
    float trigger[2]; /* L2, R2 analog 0..1 past the press point; <= 0: digital (full speed) */
    unsigned held;    /* PS1 pad bits held (libetc: L2 1, R2 2, Start 0x800, D-pad 0xF000) */
    float deadzone;   /* Modern: the stick's deadzone (0..0.9) */
    int controls;     /* PC_MAP_CONTROLS_* */
    int invert;       /* qol_map_invert: Original's stick pan follows it */
} pc_map_pad_in;

typedef struct {
    int pitch, yaw, distance, gx, gy; /* MENU5's _cameraAngles.vx / vy, _cameraDistance, _geomOffsetX / Y */
    float frac[3];                    /* Modern: yaw, pitch and distance below one unit, carried */
} pc_map_pad_view;

/* One frame: moves the view, returns MENU5's commands state (cmd: what its
 * own input set; 3 turned, 2 zoomed). */
int pc_map_pad_step(const pc_map_pad_in* in, pc_map_pad_view* v, int cmd);

#ifdef __cplusplus
}
#endif
#endif
