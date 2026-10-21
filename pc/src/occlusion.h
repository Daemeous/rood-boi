/* Occlusion in play (occlusion.c; Native_Rendering.md "Occlusion"): what
 * stands between the camera and the characters that matter (Ashley, and in
 * battle the target, the action's actors and enemies near her) fades in a
 * soft screen-space ellipse around each of them, and what still hides them
 * can show as a faint tinted silhouette. Presentation only: the native
 * passes leave a dithered share of the occluder's pixels out (render.c,
 * native_frag.glsl), so depth, the G-buffer, lighting, GI and shadows stay
 * exact for every pixel drawn and the ray-traced scene keeps every surface. */
#ifndef ROOD_OCCLUSION_H
#define ROOD_OCCLUSION_H
#ifdef __cplusplus
extern "C" {
#endif

/* Settings keys and RPC graphics, same names (pc/README.md lists them;
 * later a Lua layer may override them, as post fields are). */
enum { PC_OCCLUSION_OFF, PC_OCCLUSION_FADE, PC_OCCLUSION_FADE_SILHOUETTE, PC_OCCLUSION_SILHOUETTE };
extern int pc_occlusion; /* occlusion: off | fade | fade_silhouette | silhouette */
extern float pc_occlusion_visibility; /* share of a faded occluder's pixels kept at the ellipse's centre */
extern float pc_occlusion_radius; /* the ellipse's half width at Ashley, world units (scaled with the subject) */
extern float pc_occlusion_aspect; /* its height / width */
extern float pc_occlusion_softness; /* share of the radius the falloff to solid takes */
extern float pc_occlusion_ease; /* seconds a fade takes in or out */
extern float pc_occlusion_hold; /* seconds a subject counts as hidden after it was last seen hidden */
extern int pc_occlusion_dither; /* 0 Bayer 8 x 8, 1 interleaved gradient noise (both fixed to the screen) */
extern int pc_occlusion_subjects; /* bits: 1 Ashley, 2 the battle target, 4 the action's actors, 8 enemies near Ashley */
extern float pc_occlusion_enemy_range; /* world units from Ashley (subjects bit 8) */
extern int pc_occlusion_props; /* fade props (object meshes that are no character) too */
extern int pc_occlusion_cutscenes; /* also while an event script runs */
extern float pc_occlusion_silhouette_ashley; /* silhouette strength (share of hidden pixels tinted) */
extern float pc_occlusion_silhouette_enemy;
extern float pc_occlusion_silhouette_rim; /* how much the silhouette favours its rim (0 flat, 1 rim only) */
extern float pc_occlusion_tint_ashley[3]; /* 0..1 */
extern float pc_occlusion_tint_enemy[3];
extern const char* const pc_occlusion_names[4]; /* "off", "fade", "fade_silhouette", "silhouette" */

#define PC_OCCLUSION_SUBJECTS 4
typedef struct {
    int slot; /* actor slot (0 Ashley) */
    int enemy; /* not Ashley: the hostile tint */
    float center[3], previous[3]; /* world units, its body's middle; the game frame before */
    float half_w, half_h; /* ellipse half axes at the subject (world units) */
    float body; /* its body's horizontal radius (world units): characters' meshes near it are its own */
    float feet_y; /* world y of its feet */
    float fade, fade_previous; /* eased 0..1: the fade drawn now, and the game frame before */
    float hidden; /* eased 0..1: the subject is hidden (silhouettes) */
    int rays_blocked; /* diagnostics: of 5 */
} pc_occlusion_subject;

/* room_geometry.c func_8009723C, once per game frame for the current room
 * (after its group walk; free_camera: the editor's camera flies). */
void pc_occlusion_frame(int free_camera);
/* The subjects of the current game frame (render.c, RPC); returns how many
 * (0: occlusion off or inactive now). */
int pc_occlusion_subjects_get(const pc_occlusion_subject** out);
/* A character mesh near world point p (its root): 1 when it is one (never
 * faded); *subject: the index of the subject it belongs to, -1 none. */
int pc_occlusion_character(const float p[3], int* subject);
/* Settings: a key of the settings file (occlusion, occlusion_*) with its
 * value text (also a JSON value: "fade", 0.35, [0.6, 0.7, 0.8]); returns 1
 * when the key is occlusion's. pc_occlusion_key(i): the keys, NULL past the
 * last. pc_occlusion_json: the settings and this frame's subjects (RPC
 * occlusion). */
int pc_occlusion_setting(const char* key, const char* value);
const char* pc_occlusion_key(int index);
int pc_occlusion_json(char* out, int size);
#ifdef EOF
void pc_occlusion_settings_write(FILE* f);
#endif

#ifdef __cplusplus
}
#endif
#endif
