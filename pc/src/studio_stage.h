/* The studio's stage as a room-format block (studio_stage.c). */
#ifndef ROOD_STUDIO_STAGE_H
#define ROOD_STUDIO_STAGE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    float radius, fillet, height; /* the floor's, the curve's, the wall's */
    int grey;                     /* 0..255 */
    float facet, floor_step, wall_step;
} pc_stage_cfg;
/* The block in `block` (capacity `length`) with the stage centred at (cx, fy, cz), fy the floor's y (y is down). Returns the bytes used; 0 with `why`
 * when the config is bad or the block is too small (*need_out is then the size it wants). */
unsigned pc_stage_build(const pc_stage_cfg* cfg, float cx, float fy, float cz, unsigned char* block, unsigned length, unsigned* need_out,
    char* why, int why_size, char* info, int info_size);
/* A block with one facet that faces away from every view (nothing is drawn; the scene still has a room). */
unsigned pc_stage_build_hidden(float cx, float fy, float cz, unsigned char* block, unsigned length, char* why, int why_size, char* info,
    int info_size);
/* The block's polygons for the renderer; the count (malloc'd in *out), -1 when it is not a stage block. */
int pc_stage_polygons(const unsigned char* block, unsigned length, void** out); /* (render_polygon*: render.h) */
#ifdef __cplusplus
}
#endif
#endif
