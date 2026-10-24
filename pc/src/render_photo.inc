/* Renderer-owned photo checkpoint. Lists contain palettes and camera/projection
 * data for the picture already drawn, not the game frame at the pause boundary.
 * Deep copies are necessary: photo renders reuse both list IDs and ring slots. */
static draw_list* photo_lists;
static uint32_t photo_list_serial;
static render_mesh* photo_meshes;
static int photo_mesh_count;
/* Restored lists may still be replayed until normal game frames replace them.
 * These private meshes are not actor-cache handles, so updating or destroying
 * a live actor mesh during a photo render cannot alter the checkpoint. */
static render_mesh* photo_restored_meshes;
static int photo_restored_mesh_count;
static uint32_t photo_restored_serial;
static void photo_meshes_free(render_mesh** meshes, int* count)
{
    for (int i = 0; i < *count; ++i)
        render_mesh_destroy((*meshes)[i]);
    free(*meshes);
    *meshes = NULL;
    *count = 0;
}
static void photo_meshes_expire(void)
{
    if ((uint32_t)(frame_serial - photo_restored_serial) > TRANSIENT_FRAMES)
        photo_meshes_free(&photo_restored_meshes, &photo_restored_mesh_count);
}

static void photo_list_free(draw_list* list)
{
    free(list->instances);
    free(list->transforms);
    free(list->order);
    free(list->translucent);
    free(list->translucent_flags);
    memset(list, 0, sizeof *list);
}
void render_photo_drop(void)
{
    if (photo_lists) {
        for (int i = 0; i < LIST_RING; ++i)
            photo_list_free(&photo_lists[i]);
        free(photo_lists);
        photo_lists = NULL;
    }
    photo_meshes_free(&photo_meshes, &photo_mesh_count);
}
int render_photo_mark(void)
{
    render_photo_drop();
    photo_lists = calloc(LIST_RING, sizeof *photo_lists);
    if (!photo_lists)
        return 0;
    for (int i = 0; i < LIST_RING; ++i) {
        const draw_list* src = &lists[i];
        draw_list* dst = &photo_lists[i];
        *dst = *src;
        dst->instances = NULL;
        dst->transforms = NULL;
        dst->order = dst->translucent = NULL;
        dst->translucent_flags = NULL;
#define PHOTO_COPY(member, used, capacity_field) \
        dst->capacity_field = src->used; \
        if (src->used) { \
            dst->member = malloc((size_t)src->used * sizeof *src->member); \
            if (!dst->member) { render_photo_drop(); return 0; } \
            memcpy(dst->member, src->member, (size_t)src->used * sizeof *src->member); \
        }
        PHOTO_COPY(instances, count, capacity)
        PHOTO_COPY(transforms, transform_count, transform_capacity)
        PHOTO_COPY(order, order_used, order_capacity)
        PHOTO_COPY(translucent, translucent_used, translucent_capacity)
        PHOTO_COPY(translucent_flags, translucent_used, translucent_capacity)
#undef PHOTO_COPY
    }
    /* Mesh slots are mutable and may be deleted by actor/effect caches. Clone
     * each unique live handle, including its CPU polygons and emissive scope.
     * Some old ring entries already name expired meshes; leave those invalid
     * handles invalid rather than resurrecting geometry which was not live. */
    int original_count = mesh_count;
    render_mesh* map = calloc((size_t)original_count + 1, sizeof *map);
    photo_meshes = calloc((size_t)original_count + 1, sizeof *photo_meshes);
    if (!map || !photo_meshes) {
        free(map);
        render_photo_drop();
        return 0;
    }
    for (int i = 0; i < LIST_RING; ++i) {
        draw_list* list = &photo_lists[i];
        for (int j = 0; j < list->count; ++j) {
            render_mesh id = list->instances[j].mesh;
            mesh_slot* slot = lookup(id);
            if (!slot)
                continue;
            unsigned index = id & 0xffff;
            if (!map[index]) {
                /* Creating the clone may realloc the mesh registry, so retain
                 * only the separately allocated source arrays across the call. */
                const uint16_t* scope = slot->scope;
                render_mesh clone = render_mesh_create(slot->cpu, slot->polygons);
                if (clone)
                    photo_meshes[photo_mesh_count++] = clone;
                if (!clone || (scope && !render_mesh_emissive_scope(clone, scope))) {
                    free(map);
                    render_photo_drop();
                    return 0;
                }
                map[index] = clone;
            }
            list->instances[j].mesh = map[index];
        }
    }
    free(map);
    photo_list_serial = frame_serial;
    return 1;
}
void render_photo_restore(void)
{
    if (!photo_lists)
        return;
    for (int i = 0; i < LIST_RING; ++i) {
        photo_list_free(&lists[i]);
        lists[i] = photo_lists[i];
        memset(&photo_lists[i], 0, sizeof photo_lists[i]);
    }
    frame_serial = photo_list_serial;
    photo_meshes_free(&photo_restored_meshes, &photo_restored_mesh_count);
    photo_restored_meshes = photo_meshes;
    photo_restored_mesh_count = photo_mesh_count;
    photo_restored_serial = frame_serial;
    photo_meshes = NULL;
    photo_mesh_count = 0;
    render_photo_drop();
}
