/* Anti-aliasing (pc/README.md "Anti-aliasing"; Graphics > Lighting quality >
 * Display, settings key `aa`, RPC graphics `aa`). Every method keeps the
 * PS1's nearest-sampled texels: none of them filters textures.
 *
 *   post-process (one of): off, fxaa, smaa (SMAA 1x, edges only where the
 *     geometry breaks), on the lit image before the depth of field;
 *   msaa2 / msaa4 / msaa8: multisampled 3D geometry (coverage only, one
 *     shading sample per pixel), resolved before anything reads the target;
 *   ssaa[factor]: the internal resolution raised to factor x the view, the
 *     lit image drawn at that resolution and filtered down (aa_downsample).
 *
 * Written as one string joined by '+', e.g. "msaa4+smaa". */
#ifndef ROOD_ANTIALIAS_H
#define ROOD_ANTIALIAS_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PC_AA_OFF = 0, PC_AA_FXAA = 1, PC_AA_SMAA = 2 };
/* The post-process filter (PC_AA_*). Named pc_gl_aa since FXAA was the only
 * one: RPC graphics `aa` true/false still mean FXAA/off. */
extern int pc_gl_aa;
extern int pc_aa_msaa; /* 0 (off), 2, 4 or 8 samples */
extern float pc_aa_ssaa; /* 0 off, else 1.25-4: internal resolution / view */
enum { PC_AA_DOWNSAMPLE_AREA = 0, PC_AA_DOWNSAMPLE_LANCZOS = 1, PC_AA_DOWNSAMPLE_BILINEAR = 2 };
extern int pc_aa_downsample; /* supersampling's filter to the view */
extern int pc_aa_geometry_edges; /* SMAA: edges only where the geometry breaks (1, default) */
extern const char* const pc_aa_downsample_names[3];
/* First run (a settings file without `aa`, or none yet): the methods are
 * picked for the GPU when it is known (lighting_quality.cpp): msaa4+smaa on
 * a discrete GPU, smaa on an integrated one. Without ROOD_SETTINGS (tests)
 * the built-in FXAA stays. ROOD_AA=... overrides both. */
extern int pc_aa_autodetect_pending;
const char* pc_aa_autodetect(int integrated);

/* "msaa4+smaa" -> the settings above (unknown words fail, nothing changes;
 * "off" clears all three). Returns 1 when it parsed. */
int pc_aa_parse(const char* text);
/* The settings as such a string ("off" when none). */
const char* pc_aa_string(void);
/* The scale supersampling asks for at a view `view_h` window pixels tall
 * showing a display area `display_h` VRAM pixels tall (1-8), or 0 when
 * supersampling is off. */
int pc_aa_ssaa_scale(int view_h, int display_h);

/* SMAA's lookup textures (pc/third_party/smaa/Textures, MIT): the area
 * texture (R8G8, 160 x 560) and the search texture (R8, 64 x 16). */
const unsigned char* pc_smaa_area_texture(int* width, int* height);
const unsigned char* pc_smaa_search_texture(int* width, int* height);

#ifdef __cplusplus
}
#endif
#endif
