/* A photo render borrows the renderer as well as the game. The latter is held
 * at M, but its displayed image can be M-1 (or an interpolated image). Keep the
 * actual targets AND the recordings/native lists that describe their camera,
 * projection, depth and palettes. Regenerating M cannot restore this picture. */
extern int render_photo_mark(void);
extern void render_photo_restore(void);
extern void render_photo_drop(void);

typedef struct {
    GLuint fbo, color, normal, albedo, shade, depth;
} photo_target;
static photo_target photo_gl[2], photo_gl_depth[2];
static struct {
    recording frames[3];
    int index, newest, previous, display[4], shown[4], replayed;
    unsigned arrival[3], serial;
    float blend, r[3][3], t[3], h, ofx, ofy, fade, fade_rect[4];
    int menu_model, redraw, redraw_origin[2];
    overlay_triangle overlay[OVERLAY_MAX];
    int overlay_count;
    uint64_t overlay_cells[32];
    int stale_rects[MAX_STALE][4], stale_kinds[MAX_STALE], stale_count;
    int occlusion_on, occlusion_rect[4], surface_valid;
    int scale, vulkan;
    render_camera view;
    int view_valid;
} *photo_picture;

static void photo_gl_copy(GLuint from, GLuint to, int colors)
{
    int w = 1024 * pc_hw_scale, h = 512 * pc_hw_scale;
    gl_Disable(GL_SCISSOR_TEST);
    gl_BindFramebuffer(GL_READ_FRAMEBUFFER, from);
    gl_BindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
    for (int i = 0; i < colors; ++i) {
        gl_ReadBuffer(all_buffers[i]);
        gl_DrawBuffers(1, &all_buffers[i]);
        gl_BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    gl_BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    if (colors) {
        gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl_BindFramebuffer(GL_FRAMEBUFFER, to);
        gl_DrawBuffers(4, all_buffers);
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
}
/* Cheap request-time work: retain the selected target and its view until
 * the host poll can allocate/copy outside a budgeted Lua callback. */
int pc_hw_photo_prepare(void)
{
    render_camera view = {0};
    if (!pc_hw_active || photo_picture)
        return 0;
    int valid = pc_scene_camera_get(view.rotation, view.translation, &view.h, &view.ofx, &view.ofy);
    photo_restored_camera = view;
    photo_restored_camera_valid = valid;
    photo_preparing = 1;
    return 1;
}
void pc_hw_photo_drop(void)
{
    photo_preparing = 0;
#ifdef ROOD_VULKAN
    if (photo_picture && photo_picture->vulkan)
        pc_vk_hw_photo_drop();
    else
#endif
    if (photo_picture) {
        for (int i = 0; i < 2; ++i) {
            photo_target* p = &photo_gl[i];
            GLuint textures[] = { p->color, p->normal, p->albedo, p->shade, p->depth };
            gl_DeleteTextures(5, textures);
            gl_DeleteFramebuffers(1, &p->fbo);
            gl_DeleteTextures(1, &photo_gl_depth[i].depth);
            gl_DeleteFramebuffers(1, &photo_gl_depth[i].fbo);
        }
        memset(photo_gl, 0, sizeof photo_gl);
        memset(photo_gl_depth, 0, sizeof photo_gl_depth);
    }
    free(photo_picture);
    photo_picture = NULL;
    render_photo_drop();
}
int pc_hw_photo_mark(void)
{
    if (photo_picture) {
#ifdef ROOD_VULKAN
        if (photo_picture->vulkan != !!pc_vk_on())
            return 0;
#endif
        return pc_hw_active && photo_picture->scale == pc_hw_scale;
    }
    if (!pc_hw_active)
        return 0;
    flush();
    ms_end();
    photo_picture = calloc(1, sizeof *photo_picture);
    if (!photo_picture)
        return 0;
    photo_picture->scale = pc_hw_scale;
    photo_picture->view_valid = pc_scene_camera_get(photo_picture->view.rotation,
        photo_picture->view.translation, &photo_picture->view.h,
        &photo_picture->view.ofx, &photo_picture->view.ofy);
#ifdef ROOD_VULKAN
    photo_picture->vulkan = pc_vk_on();
    if (photo_picture->vulkan) {
        if (!pc_vk_hw_photo_mark()) {
            pc_hw_photo_drop();
            return 0;
        }
    } else
#endif
    {
        while (gl_GetError() != GL_NO_ERROR) {}
        for (int i = 0; i < 2; ++i) {
            photo_target* p = &photo_gl[i];
            make_target(&p->color, &p->fbo);
            make_depth(p->fbo, &p->depth);
            make_gbuffer(p->fbo, &p->normal, &p->albedo, &p->shade);
            if (gl_CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                pc_hw_photo_drop();
                return 0;
            }
            gl_GenFramebuffers(1, &photo_gl_depth[i].fbo);
            make_depth(photo_gl_depth[i].fbo, &photo_gl_depth[i].depth);
            gl_DrawBuffer(GL_NONE);
            gl_ReadBuffer(GL_NONE);
            if (gl_CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                pc_hw_photo_drop();
                return 0;
            }
            photo_gl_copy(i ? replay.fbo : target_fbo, p->fbo, 4);
            photo_gl_copy(i ? before_fbo : room_depth_fbo, photo_gl_depth[i].fbo, 0);
        }
    }
    if (!render_photo_mark() || (!photo_picture->vulkan && gl_GetError() != GL_NO_ERROR)) {
        pc_hw_photo_drop();
        return 0;
    }
#define KEEP(field, value) photo_picture->field = value
#define COPY(field, value) memcpy(photo_picture->field, value, sizeof photo_picture->field)
    COPY(frames, recordings);
    KEEP(index, recording_index); KEEP(newest, newest); KEEP(previous, previous);
    COPY(arrival, arrival); COPY(display, display_rect); COPY(shown, shown_rect);
    KEEP(serial, frame_serial); KEEP(replayed, presenting_replay); KEEP(blend, present_blend);
    COPY(r, replay_r); COPY(t, replay_t); KEEP(h, replay_h); KEEP(ofx, replay_ofx); KEEP(ofy, replay_ofy);
    KEEP(fade, present_fade); COPY(fade_rect, present_fade_rect); KEEP(menu_model, present_menu_model);
    KEEP(redraw, redraw_shown); COPY(redraw_origin, redraw_origin);
    COPY(overlay, overlay); COPY(overlay_cells, overlay_cells); KEEP(overlay_count, overlay_count);
    COPY(stale_rects, stale_rects); COPY(stale_kinds, stale_kinds); KEEP(stale_count, stale_count);
    KEEP(occlusion_on, occlusion_on); COPY(occlusion_rect, occlusion_rect); KEEP(surface_valid, surface_valid);
#undef COPY
#undef KEEP
    photo_preparing = 0;
    return 1;
}
int pc_hw_photo_restore(void)
{
    if (!photo_picture || !pc_hw_active || photo_picture->scale != pc_hw_scale)
        return 0;
#ifdef ROOD_VULKAN
    if (photo_picture->vulkan != !!pc_vk_on())
        return 0;
#endif
    /* Guest VRAM has already rewound. Resync only the sampled texture before
     * restoring the full-resolution backing targets and recording history. */
    pc_hw_resync();
#ifdef ROOD_VULKAN
    if (photo_picture->vulkan) {
        if (!pc_vk_on() || !pc_vk_hw_photo_restore())
            return 0;
    } else
#endif
    {
        for (int i = 0; i < 2; ++i) {
            photo_gl_copy(photo_gl[i].fbo, i ? replay.fbo : target_fbo, 4);
            photo_gl_copy(photo_gl_depth[i].fbo, i ? before_fbo : room_depth_fbo, 0);
        }
    }
    render_photo_restore();
#define RESTORE(value, field) value = photo_picture->field
#define COPY(value, field) memcpy(value, photo_picture->field, sizeof photo_picture->field)
    COPY(recordings, frames);
    RESTORE(recording_index, index); RESTORE(newest, newest); RESTORE(previous, previous);
    COPY(arrival, arrival); COPY(display_rect, display); COPY(shown_rect, shown);
    RESTORE(frame_serial, serial); RESTORE(presenting_replay, replayed); RESTORE(present_blend, blend);
    COPY(replay_r, r); COPY(replay_t, t); RESTORE(replay_h, h); RESTORE(replay_ofx, ofx); RESTORE(replay_ofy, ofy);
    RESTORE(present_fade, fade); COPY(present_fade_rect, fade_rect); RESTORE(present_menu_model, menu_model);
    RESTORE(redraw_shown, redraw); COPY(redraw_origin, redraw_origin);
    COPY(overlay, overlay); COPY(overlay_cells, overlay_cells); RESTORE(overlay_count, overlay_count);
    COPY(stale_rects, stale_rects); COPY(stale_kinds, stale_kinds); RESTORE(stale_count, stale_count);
    RESTORE(occlusion_on, occlusion_on); COPY(occlusion_rect, occlusion_rect); RESTORE(surface_valid, surface_valid);
#undef COPY
#undef RESTORE
    /* Keep this actual presentation until a NEW game draw, not until the next
     * paused-window repaint (whose vsync may differ from the original image). */
    photo_restored_camera = photo_picture->view;
    photo_restored_camera_valid = photo_picture->view_valid;
    photo_restored = 1;
    force_replay = 1; /* discard replay memoization from the borrowed renders */
    pc_hw_photo_drop();
    return 1;
}
