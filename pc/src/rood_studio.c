/* rood-studio (pc/docs/plans/2026-11-24-standalone-studio-design.md): the standalone model studio's app. Entered from main() once the window and
 * renderer are up, instead of the game: no title, no save, no room, no overlays, no game loop. A model is read from the disc by the native
 * decoders (native_model/), its texture and palette go into the emulated VRAM the way the loader's LoadImage puts them, and every frame is the
 * frame's items alone (draw_items.h, as the ending's are): one native list, drawn by the renderer. */
#include "draw_items.h"
#include "native_model/native_model.h"
#include "render.h"
#include "studio.h"
#include "studio_stage.h"
#include "ui/item_names.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int pc_vsync_wait_frames(void) __attribute__((weak));
extern int VSync(int);
extern char pc_present_capture[400] __attribute__((weak));
extern void pc_log(const char*);
extern void pc_emissive_model_mesh(uint32_t mesh, uint32_t model, uint64_t key) __attribute__((weak));
extern int pc_live_camera_active(void) __attribute__((weak));
extern unsigned pc_presents __attribute__((weak)); /* gl_present.c: images presented */
extern int pc_gte_widescreen;
extern void pc_scene_room(const void* data);
extern void pc_scene_camera(const void* view);
extern void pc_studio_app_room(unsigned char* block, unsigned length);
extern int pc_studio_app_look(const char* name);
extern void pc_studio_app_stage(float*, float*, float*, int*, float*, float*, float*, int*);
extern void SetGeomOffset(int, int);
extern void SetGeomScreen(int);
/* boot_sdk.c's own: what LoadImage, PutDrawEnv and PutDispEnv hand the renderer */
extern void pc_vram_transfer(int x, int y, int w, int h, void* data, int to_vram);
extern void pc_vram_clear(int x, int y, int w, int h, int r, int g, int b);
extern void pc_draw_env(int x, int y, int w, int h, int ofs_x, int ofs_y, int tpage);
extern void pc_display_env(int x, int y, int w, int h);

/* The game's 24 texture slots (vs_actors_textureSlotOrigins, vs_actors_textureSlotRowOffsets; rood_studio_smoke.py checks them against the game):
 * x in 64-pixel pages, y in 256-line pages, the slot's own row offset in 64-line steps. */
static const short slot_origin[24][2] = { { 0, 1 }, { 0, 1 }, { 2, 1 }, { 2, 1 }, { 10, 0 }, { 10, 0 }, { 10, 0 }, { 10, 0 }, { 11, 0 }, { 11, 0 }, { 11, 0 },
    { 11, 0 }, { 13, 1 }, { 13, 1 }, { 13, 1 }, { 13, 1 }, { 14, 1 }, { 14, 1 }, { 14, 1 }, { 14, 1 }, { 15, 1 }, { 15, 1 }, { 15, 1 }, { 15, 1 } };
static const unsigned char slot_rows[24] = { 2, 3, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };

typedef struct {
    pc_nm_model model;
    int texture_slot, actor_slot; /* the texture slot (page, rows) and the actor slot (CLUT rows) the model stands in */
    int v_add, u_add;
    unsigned tpage, clut;
    render_mesh mesh;
    int bones;
    uint64_t key;
    unsigned char* zud; /* an enemy's file (its weapon and shield are parts of it) */
    unsigned zud_size;
} loaded_model;

/* The enemy's ZUD file name from the studio's catalog (pc/assets/studio/catalog.json: {"id": 14, "file": "Z009U01"}, ids and file names only). */
static int enemy_file(int id, char* out, size_t size)
{
    const char* path = getenv("ROOD_STUDIO_CATALOG");
    FILE* f = fopen(path && *path ? path : "pc/assets/studio/catalog.json", "rb");
    if (!f)
        return 0;
    char* text = malloc(1 << 20);
    size_t n = text ? fread(text, 1, (1 << 20) - 1, f) : 0;
    fclose(f);
    if (!text)
        return 0;
    text[n] = 0;
    char key[40];
    snprintf(key, sizeof key, "{\"id\": %d, \"file\": \"", id);
    char* at = strstr(text, key);
    int ok = 0;
    if (at) {
        at += strlen(key);
        char* end = strchr(at, '"');
        if (end && (size_t)(end - at) < size)
            memcpy(out, at, (size_t)(end - at)), out[end - at] = 0, ok = 1;
    }
    free(text);
    return ok;
}

static int disc(const char* path, unsigned char** data, unsigned* size)
{
    if (pc_ui_disc_read(path, data, size))
        return 1;
    fprintf(stderr, "[rood-studio] cannot read %s from the disc image (ROOD_DISC, else disks/SLUS-01040.bin)\n", path);
    return 0;
}

/* GetTPage / GetClut as libgpu has them. */
static unsigned tpage_word(int depth, int x, int y) { return (unsigned)(((depth & 3) << 7) | ((y & 0x100) >> 4) | ((x & 0x3ff) >> 6)); }
static unsigned clut_word(int x, int y) { return (unsigned)((y << 6) | ((x >> 4) & 0x3f)); }

static void upload(int x, int y, int w_halfwords, int h, const void* data)
{
    pc_vram_transfer(x, y, w_halfwords, h, (void*)data, 1);
}

/* The texture and palette of `texture_from` (the model itself, or the model whose texture it shares) into the slot. */
static void put_texture(loaded_model* lm, const pc_nm_model* texture_from)
{
    if (!texture_from->has_texture)
        return;
    int slot = lm->texture_slot;
    int x = slot_origin[slot][0] << 6, y = (slot_origin[slot][1] << 8) + (slot_rows[slot] << 6);
    upload(x, y, texture_from->texture_width / 2, texture_from->texture_height, texture_from->texels);
    /* the palette, at the actor slot's two CLUT rows (the loader's vs_main_loadClut slots index + 4 and index + 0x16) */
    upload(768, 228 + lm->actor_slot, texture_from->colours, 1, texture_from->palette);
    upload(768, 246 + lm->actor_slot, texture_from->colours, 1, texture_from->palette);
    lm->tpage = tpage_word(texture_from->eight_bit ? 1 : 0, x, y);
    lm->clut = clut_word(768, 228 + lm->actor_slot);
}

/* The game's table from a weapon's material to the palette it takes of the file's seven (_weaponMaterialPaletteMap; rood_studio_smoke.py checks the first entries). */
static const unsigned char material_palette[16] = { 0, 5, 6, 1, 0, 2, 3, 4, 6, 15, 16, 9, 10, 7, 12, 17 };

/* The mesh, key and bone count of a decoded model (the page and CLUT words are set). */
static int make_mesh(loaded_model* lm, const char* what)
{
    render_polygon* polygons = NULL;
    int count = pc_nm_polygons(&lm->model, lm->u_add, lm->v_add, &polygons);
    if (count <= 0) {
        fprintf(stderr, "[rood-studio] %s: the polygons are not valid\n", what);
        return -1;
    }
    lm->mesh = render_mesh_create(polygons, count);
    free(polygons);
    lm->key = pc_nm_content_key(&lm->model, lm->u_add, lm->v_add);
    if (lm->mesh && pc_emissive_model_mesh)
        pc_emissive_model_mesh(lm->mesh, pc_nm_mesh_hash(&lm->model, lm->u_add, lm->v_add), lm->key);
    int most = 0;
    for (int v = 0; v < lm->model.vertices; v++)
        most = lm->model.bone_of[v] + 1 > most ? lm->model.bone_of[v] + 1 : most;
    lm->bones = most;
    return lm->mesh ? 0 : -1;
}

/* A weapon (or shield) file into the loader's weapon slot `wep_slot` (0..19: its texture's place), held by the bone `unk11` names (0xF0 weapon, 0xF1 shield) of
 * the actor in `actor_slot`: _parseWep and _loadMaterialPalette. */
static int load_wep_part(loaded_model* lm, const unsigned char* data, unsigned size, int wep_slot, int unk11, int material)
{
    char err[160];
    memset(lm, 0, sizeof *lm);
    lm->actor_slot = 9;
    if (pc_nm_wep_decode(data, size, &lm->model, err, sizeof err)) {
        fprintf(stderr, "[rood-studio] weapon: %s\n", err);
        return -1;
    }
    const pc_nm_model* w = &lm->model;
    lm->texture_slot = wep_slot;
    lm->v_add = 0xE0, lm->u_add = (wep_slot & 1) * 64; /* the loader's fix-up: the page row 0xE0, the page column the slot's parity */
    upload((wep_slot / 2) * 64 + (wep_slot & 1) * 32, 0xE0, w->texture_width / 2, 32, w->texels);
    lm->tpage = tpage_word(1, (wep_slot / 2) * 64, 0);
    /* the CLUT slot: unk11 + 2 * actor slot + 16, its row and column from it (the first palette, then the material's after it) */
    int c = (unk11 + 2 * lm->actor_slot + 16) & 255;
    int palette = material >= 0 && material < 16 ? material_palette[material] : 0;
    if (palette > 6)
        palette = 0;
    unsigned first = w->wep_first_palette_bytes, second = w->wep_palette_bytes;
    unsigned char* clut = calloc(1, first + second);
    memcpy(clut, w->palette, first);
    memcpy(clut + first, w->wep_palettes + (size_t)palette * second, second);
    int x = 928 + (c & 1) * 48, y = 228 + (c >> 1);
    upload(x, y, (int)(first + second) / 2, 1, clut);
    free(clut);
    lm->clut = clut_word(x, y);
    return make_mesh(lm, "weapon");
}

static int load_model(loaded_model* lm, const char* kind, int id, int texture_slot)
{
    memset(lm, 0, sizeof *lm);
    lm->texture_slot = texture_slot;
    lm->actor_slot = 9; /* the in-game studio's (the highest free actor slot) */
    char path[64], err[160];
    unsigned char *file = NULL, *base_file = NULL;
    unsigned size = 0, base_size = 0;
    const unsigned char* shp = NULL;
    size_t shp_size = 0;
    if (!strcmp(kind, "weapon")) {
        snprintf(path, sizeof path, "OBJ/%02X.WEP", id);
        if (!disc(path, &file, &size))
            return -1;
        int r = load_wep_part(lm, file, size, 2, 0xF0, 0); /* the in-game studio's weapon slot (an even one) */
        free(file);
        return r;
    }
    if (!strcmp(kind, "enemy")) {
        char name[16];
        if (!enemy_file(id, name, sizeof name)) {
            fprintf(stderr, "[rood-studio] the catalog has no enemy %d\n", id);
            return -1;
        }
        snprintf(path, sizeof path, "MAP/%s.ZUD", name);
        if (!disc(path, &file, &size))
            return -1;
        unsigned at = size > 16 ? (unsigned)(file[8] | file[9] << 8 | file[10] << 16 | (unsigned)file[11] << 24) : 0;
        if (!at || at >= size) {
            free(file);
            return -1;
        }
        shp = file + at, shp_size = size - at;
        lm->zud = file; /* kept: its weapon and shield are parts of it */
        lm->zud_size = size;
    } else {
        snprintf(path, sizeof path, "OBJ/%02X.SHP", id);
        if (!disc(path, &file, &size))
            return -1;
        shp = file, shp_size = size;
    }
    int r = pc_nm_shp_decode(shp, shp_size, &lm->model, err, sizeof err);
    if (!lm->zud)
        free(file);
    if (r) {
        fprintf(stderr, "[rood-studio] %s: %s\n", path, err);
        return -1;
    }
    pc_nm_model base;
    memset(&base, 0, sizeof base);
    int have_base = 0;
    if (lm->model.shared_model >= 0) { /* the model whose texture this one shares, decoded for its texture alone */
        snprintf(path, sizeof path, "OBJ/%02X.SHP", lm->model.shared_model);
        if (!disc(path, &base_file, &base_size) || pc_nm_shp_decode(base_file, base_size, &base, err, sizeof err)) {
            free(base_file);
            return -1;
        }
        free(base_file);
        have_base = 1;
    }
    lm->v_add = slot_rows[texture_slot] << 6;
    put_texture(lm, have_base ? &base : &lm->model);
    if (have_base)
        pc_nm_free(&base);
    return make_mesh(lm, path);
}

static void free_model(loaded_model* lm)
{
    if (lm->mesh)
        render_mesh_destroy(lm->mesh);
    pc_nm_free(&lm->model);
    free(lm->zud);
    memset(lm, 0, sizeof *lm);
}

/* The studio's view of a model (studio.cpp put_camera, the editor's photo camera): the GTE rows with the pixel aspect in x. */
typedef struct {
    float yaw, elevation, distance, fov; /* degrees */
    float target[3];
} view_t;
static void make_camera(const view_t* v, render_camera* cam)
{
    const float pi = 3.14159265f;
    float yaw = v->yaw * pi / 180, pitch = -v->elevation * pi / 180;
    float f[3] = { cosf(pitch) * sinf(yaw), -sinf(pitch), cosf(pitch) * cosf(yaw) };
    float r[3] = { f[2], 0, -f[0] };
    float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
    r[0] /= rl, r[2] /= rl;
    float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    float eye[3];
    for (int k = 0; k < 3; k++)
        eye[k] = v->target[k] - f[k] * v->distance;
    const float aspect = 1.126f; /* what the game's view matrix carries in x (the pixel aspect, 4:3 on a 320 x 224 picture) */
    for (int k = 0; k < 3; k++) {
        cam->rotation[0][k] = r[k] * aspect, cam->rotation[1][k] = -u[k], cam->rotation[2][k] = f[k];
    }
    for (int a = 0; a < 3; a++)
        cam->translation[a] = -(cam->rotation[a][0] * eye[0] + cam->rotation[a][1] * eye[1] + cam->rotation[a][2] * eye[2]);
    cam->h = 312.0f * tanf(20.0f * pi / 180) / tanf(v->fov * 0.5f * pi / 180);
    cam->ofx = 160, cam->ofy = 112;
    cam->widescreen = pc_gte_widescreen;
}

/* The field's two frame buffers (vs_render_configureFieldDisplay 320 x 240): draw into one, show the other. */
static int background = 30;
static void screen_init(void)
{
    SetGeomOffset(160, 112);
    SetGeomScreen(312);
}
static void put_frame(int buffer)
{
    int x = buffer ? 320 : 0;
    pc_display_env(x, 0, 320, 224);
    pc_draw_env(x, 0, 320, 224, x, 0, 10);
    pc_vram_clear(x, 0, 320, 224, background, background, background);
}

/* ---- the in-game studio's dump (render_mesh_reference, RPC studio dump_model): what the renderer was given for a model the game loaded ---- */

typedef struct {
    int polygons, bones, override_flags;
    unsigned tpage, clut, colour;
    render_polygon* polygon;
    render_transform palette[RENDER_MAX_BONES];
    render_camera camera;
} reference_t;

static int read_reference(const char* path, reference_t* r)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return -1;
    int32_t head[8];
    if (fread(head, sizeof head, 1, f) != 1 || head[0] != 0x31524d52 || head[7] != (int32_t)sizeof(render_polygon) || head[1] <= 0 || head[2] > RENDER_MAX_BONES) {
        fclose(f);
        return -1;
    }
    r->polygons = head[1], r->bones = head[2], r->override_flags = head[3];
    r->tpage = (unsigned)head[4], r->clut = (unsigned)head[5], r->colour = (unsigned)head[6];
    r->polygon = malloc((size_t)r->polygons * sizeof(render_polygon));
    int ok = fread(r->polygon, sizeof(render_polygon), (size_t)r->polygons, f) == (size_t)r->polygons
        && fread(r->palette, sizeof(render_transform), (size_t)r->bones, f) == (size_t)r->bones && fread(&r->camera, sizeof r->camera, 1, f) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

/* Decode equality: every polygon of the model as the studio decodes it against the game's, the page and CLUT words, the content key. */
static int compare_reference(const loaded_model* lm, const reference_t* r, unsigned long long expect_key, int have_key)
{
    render_polygon* mine = NULL;
    int count = pc_nm_polygons(&lm->model, lm->u_add, lm->v_add, &mine);
    int bad = 0;
    long diff[8] = { 0 };
    if (count != r->polygons) {
        fprintf(stderr, "[rood-studio] compare: %d polygons here, %d in the game\n", count, r->polygons);
        bad = 1;
    }
    for (int i = 0; i < count && i < r->polygons; i++) {
        const render_polygon *a = &mine[i], *b = &r->polygon[i];
        int corners = a->flags & RENDER_POLY_QUAD ? 4 : 3;
        for (int k = 0; k < corners; k++) {
            for (int c = 0; c < 3; c++)
                diff[0] += a->position[k][c] != b->position[k][c];
            diff[1] += a->uv[k][0] != b->uv[k][0] || a->uv[k][1] != b->uv[k][1];
            diff[2] += memcmp(a->rgb[k], b->rgb[k], 3) != 0;
            diff[3] += a->bone[k] != b->bone[k];
        }
        diff[4] += a->code != b->code, diff[5] += a->flags != b->flags, diff[6] += a->bias != b->bias, diff[7] += a->tpage != b->tpage || a->clut != b->clut;
    }
    for (int i = 0; i < 8; i++)
        bad |= diff[i] != 0;
    int page_ok = (lm->tpage == r->tpage) && (lm->clut == r->clut);
    int key_ok = !have_key || lm->key == expect_key;
    fprintf(stderr,
        "[rood-studio] compare: polygons %d/%d, differences: position %ld uv %ld colour %ld bone %ld code %ld flags %ld bias %ld page %ld; page %04x/%04x clut %04x/%04x %s; key %010llx %s\n",
        count, r->polygons, diff[0], diff[1], diff[2], diff[3], diff[4], diff[5], diff[6], diff[7], lm->tpage, r->tpage, lm->clut, r->clut,
        page_ok ? "same" : "DIFFER", (unsigned long long)lm->key, key_ok ? "same" : "DIFFERS");
    free(mine);
    return bad || !page_ok || !key_ok;
}

/* The skeleton against the game's: the animation the game started, decoded here, at the time that fits the game's bone matrices best (the game's
 * render replays a few frames from where it held, so the time is not known), every bone within a few units. */
static int compare_pose(const loaded_model* lm, const reference_t* r, int animation, float tolerance)
{
    if (!lm->model.bone || lm->model.wep || r->bones > lm->model.bones)
        return 0;
    pc_nm_table common, battle;
    memset(&common, 0, sizeof common), memset(&battle, 0, sizeof battle);
    if (lm->zud)
        for (int part = 3; part < 5; part++) {
            const unsigned char* at = lm->zud + 8 + 8 * part;
            unsigned offset = (unsigned)(at[0] | at[1] << 8 | at[2] << 16 | (unsigned)at[3] << 24), length = (unsigned)(at[4] | at[5] << 8 | at[6] << 16 | (unsigned)at[7] << 24);
            if (length > 8 && offset + length <= lm->zud_size)
                pc_nm_table_from_block(lm->zud + offset, length, part == 3 ? &common : &battle);
        }
    pc_nm_table table;
    int index;
    if (pc_nm_find_animation(&lm->model, common.bytes ? &common : NULL, battle.bytes ? &battle : NULL, animation, &table, &index)) {
        fprintf(stderr, "[rood-studio] pose: no animation %d in the model's tables\n", animation);
        return 1;
    }
    pc_nm_anim* a = pc_nm_anim_open(&lm->model, &table, index);
    if (!a)
        return 1;
    int frames = pc_nm_anim_frames(a), best_time = 0, best_within = -1;
    float best_worst = 1e30f;
    char odd[160] = "";
    for (int t = 1; t <= (frames < 400 ? frames : 400) + 1; t++) {
        pc_nm_pose pose;
        pc_nm_anim_pose(a, t, &pose);
        for (int k = 0; k < 3; k++)
            pose.position[k] = r->palette[0].translation[k] - pose.offset[k];
        render_transform out[RENDER_MAX_BONES];
        if (pc_nm_pose_transforms(&lm->model, &pose, out) < 0)
            break;
        int within = 0;
        float worst = 0;
        char list[160] = "";
        for (int i = 0; i < r->bones; i++) {
            float e = 0;
            for (int c = 0; c < 3; c++)
                e = fmaxf(e, fabsf(out[i].translation[c] - r->palette[i].translation[c]));
            if (e <= tolerance)
                within++;
            else
                snprintf(list + strlen(list), sizeof list - strlen(list), " %d", i);
            worst = fmaxf(worst, e);
        }
        if (within > best_within || (within == best_within && worst < best_worst))
            best_within = within, best_worst = worst, best_time = t, snprintf(odd, sizeof odd, "%s", list);
    }
    pc_nm_anim_close(a);
    fprintf(stderr, "[rood-studio] pose: animation %d (%d frames), the closest time %d: %d of %d bones within %.0f units (largest error %.2f); not matched:%s\n",
        animation, frames, best_time, best_within, r->bones, tolerance, best_worst, best_within == r->bones ? " none" : odd);
    return best_within != r->bones;
}

/* Every model the catalog names decodes (and every one's polygons are valid): the sweep rood_studio_smoke.py runs. */
static int check_catalog(void)
{
    const char* path = getenv("ROOD_STUDIO_CATALOG");
    FILE* f = fopen(path && *path ? path : "pc/assets/studio/catalog.json", "rb");
    if (!f)
        return 1;
    char* text = malloc(1 << 20);
    size_t n = fread(text, 1, (1 << 20) - 1, f);
    fclose(f);
    text[n] = 0;
    int failures = 0, characters = 0, weapons = 0, enemies = 0;
    for (int pass = 0; pass < 3; pass++) {
        const char* name = pass == 0 ? "\"characters\"" : pass == 1 ? "\"weapons\"" : "\"enemies\"";
        char* at = strstr(text, name);
        if (!at)
            continue;
        at = strchr(at, '[') + 1;
        char* end = pass == 2 ? strchr(at, ']') : strchr(at, ']');
        if (pass == 2) { /* the enemies are objects: find the array's real end */
            int depth = 1;
            for (end = at; *end && depth; end++)
                depth += *end == '[' ? 1 : *end == ']' ? -1 : 0;
            --end;
        }
        for (char* p = at; p < end;) {
            int id = -1;
            char file[16] = "";
            if (pass == 2) {
                char* q = strstr(p, "{\"id\": ");
                if (!q || q >= end)
                    break;
                sscanf(q, "{\"id\": %d, \"file\": \"%15[^\"]", &id, file);
                p = q + 8;
            } else {
                while (p < end && (*p < '0' || *p > '9'))
                    p++;
                if (p >= end)
                    break;
                id = (int)strtol(p, &p, 10);
            }
            loaded_model lm;
            int r = load_model(&lm, pass == 0 ? "character" : pass == 1 ? "weapon" : "enemy", id, 8);
            if (r)
                fprintf(stderr, "[rood-studio] catalog: %s %d does not load\n", pass == 0 ? "character" : pass == 1 ? "weapon" : "enemy", id), failures++;
            else {
                pc_nm_free(&lm.model);
                render_mesh_destroy(lm.mesh);
                pass == 0 ? characters++ : pass == 1 ? weapons++ : enemies++;
            }
        }
    }
    free(text);
    fprintf(stderr, "[rood-studio] catalog: %d characters, %d weapons, %d enemies decode; %d failures\n", characters, weapons, enemies, failures);
    return failures != 0;
}

/* An enemy's weapon (the second mesh the game's studio dumped, <reference>.1): its polygons, page and CLUT words, key, and its bones from the holder's
 * hand bone in the game's palette. */
static int compare_equipment(const loaded_model* body, const char* kind, const char* reference_path)
{
    if (strcmp(kind, "enemy") || !body->zud)
        return 0;
    char second[512];
    snprintf(second, sizeof second, "%s", reference_path);
    size_t n = strlen(second);
    if (n < 2 || second[n - 1] != '0')
        return 0;
    second[n - 1] = '1';
    reference_t ref;
    memset(&ref, 0, sizeof ref);
    if (read_reference(second, &ref))
        return 0;
    const unsigned char* z = body->zud;
    unsigned offset = (unsigned)(z[16] | z[17] << 8 | z[18] << 16 | (unsigned)z[19] << 24), length = (unsigned)(z[20] | z[21] << 8 | z[22] << 16 | (unsigned)z[23] << 24);
    if (!z[1] || !length || offset + length > body->zud_size) {
        fprintf(stderr, "[rood-studio] equipment: the game drew a weapon the ZUD does not hold\n");
        return 1;
    }
    /* the game's slot for it: the page column says which pair of slots, the first polygon's u which of the two */
    int status = 1;
    for (int slot = 0; slot < 20 && status; slot++) {
        if (slot / 2 != (int)(ref.tpage & 15))
            continue;
        loaded_model w;
        if (load_wep_part(&w, z + offset, length, slot, 0xF0, z[3]))
            continue;
        render_polygon* mine = NULL;
        int count = pc_nm_polygons(&w.model, w.u_add, w.v_add, &mine);
        int same = count == ref.polygons && count > 0 && mine[0].uv[0][0] == ref.polygon[0].uv[0][0];
        free(mine);
        if (same) {
            fprintf(stderr, "[rood-studio] weapon in slot %d: ", slot);
            status = compare_reference(&w, &ref, 0, 0);
            /* the hand's matrix is the game's own (its bone 0), the others come from it: the chain, not the pose, is what is compared (the hand bone is not
             * one of the body mesh's, so the body's dump has no matrix for it) */
            render_transform chain[RENDER_MAX_BONES];
            if (pc_nm_find_bone(&body->model, 0xF0) >= 0 && pc_nm_weapon_transforms(&w.model, &ref.palette[0], chain) >= 0) {
                float worst = 0;
                for (int i = 0; i < ref.bones && i < w.model.bones; i++)
                    for (int a = 0; a < 3; a++) {
                        worst = fmaxf(worst, fabsf(chain[i].translation[a] - ref.palette[i].translation[a]));
                        for (int c = 0; c < 3; c++)
                            worst = fmaxf(worst, 100 * fabsf(chain[i].rotation[a][c] - ref.palette[i].rotation[a][c]));
                    }
                fprintf(stderr, "[rood-studio] the weapon's bones from its hand matrix: largest difference %.3f (rotation x 100, place in units) over %d bones\n", worst, ref.bones);
                status |= worst > 2.0f;
            } else
                status = 1;
        }
        free_model(&w);
    }
    if (status)
        fprintf(stderr, "[rood-studio] equipment: the weapon does not match the game's\n");
    return status;
}

static int run_fixed(int argc, char** argv)
{
    const char* kind = "character";
    int id = 3, frames = 0, slot = 8, check = 0, animation = 0;
    float tolerance = 4;
    const char *dump = NULL, *reference_path = NULL, *look = "neutral";
    unsigned long long expect_key = 0;
    int have_key = 0;
    view_t view = { 20, 12, 0, 40, { 0, 0, 0 } };
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--character") && i + 1 < argc)
            kind = "character", id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--enemy") && i + 1 < argc)
            kind = "enemy", id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--weapon") && i + 1 < argc)
            kind = "weapon", id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump = argv[++i];
        else if (!strcmp(argv[i], "--slot") && i + 1 < argc)
            slot = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc)
            view.yaw = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--elevation") && i + 1 < argc)
            view.elevation = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--look") && i + 1 < argc)
            look = argv[++i];
        else if (!strcmp(argv[i], "--reference") && i + 1 < argc)
            reference_path = argv[++i];
        else if (!strcmp(argv[i], "--tolerance") && i + 1 < argc)
            tolerance = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--animation") && i + 1 < argc)
            animation = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--expect-key") && i + 1 < argc)
            expect_key = strtoull(argv[++i], NULL, 16), have_key = 1;
        else if (!strcmp(argv[i], "--check-catalog"))
            check = 1;
    }
    screen_init();
    if (check)
        return check_catalog();
    loaded_model lm;
    if (load_model(&lm, kind, id, slot)) {
        fprintf(stderr, "[rood-studio] cannot load %s %d\n", kind, id);
        return 1;
    }
    fprintf(stderr, "[rood-studio] %s %d: %d bones, %d vertices, %d polygons, key %010llx\n", kind, id, lm.model.bones, lm.model.vertices,
        lm.model.counts[0] + lm.model.counts[1] + lm.model.counts[2], (unsigned long long)lm.key);
    reference_t ref;
    memset(&ref, 0, sizeof ref);
    int have_ref = reference_path && read_reference(reference_path, &ref) == 0;
    if (reference_path && !have_ref) {
        fprintf(stderr, "[rood-studio] cannot read the reference %s\n", reference_path);
        return 1;
    }
    int status = 0;
    if (have_ref && strcmp(kind, "weapon")) {
        /* the game puts a model in the first free texture slot of its kind: the slot the reference stands in, from its page and how far its texture
         * coordinates were moved down the page */
        render_polygon* raw = NULL;
        if (pc_nm_polygons(&lm.model, 0, 0, &raw) > 0) {
            int dv = (ref.polygon[0].uv[0][1] - raw[0].uv[0][1]) & 255;
            for (int candidate = 0; candidate < 24; candidate++)
                if ((slot_rows[candidate] << 6) == dv && slot_origin[candidate][0] == (int)(ref.tpage & 15) && candidate != slot) {
                    pc_nm_free(&lm.model);
                    render_mesh_destroy(lm.mesh);
                    slot = candidate;
                    if (load_model(&lm, kind, id, slot)) {
                        free(raw);
                        return 1;
                    }
                    break;
                }
        }
        free(raw);
    }
    if (have_ref) {
        status |= compare_reference(&lm, &ref, expect_key, have_key);
        status |= compare_equipment(&lm, kind, reference_path);
        if (lm.model.bone && !lm.model.wep)
            status |= compare_pose(&lm, &ref, animation, tolerance);
        if (!dump)
            return status;
    }
    pc_nm_pose pose;
    pc_nm_pose_rest(&lm.model, &pose);
    render_transform palette[RENDER_MAX_BONES];
    if (have_ref) /* the game's own pose, to draw what it drew */
        memcpy(palette, ref.palette, sizeof palette);
    else if (lm.model.wep) /* a weapon: no skeleton of its own (its holder's hand carries it) */
        for (int i = 0; i < RENDER_MAX_BONES; i++) {
            memset(&palette[i], 0, sizeof palette[i]);
            palette[i].rotation[0][0] = palette[i].rotation[1][1] = palette[i].rotation[2][2] = 1;
        }
    else if (pc_nm_pose_transforms(&lm.model, &pose, palette) < 0)
        return 1;
    /* frame the model: the box of its vertices through the pose */
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int v = 0; v < lm.model.vertices; v++) {
        const render_transform* t = &palette[lm.model.bone_of[v]];
        for (int a = 0; a < 3; a++) {
            float w = t->translation[a] + t->rotation[a][0] * lm.model.vertex[v][0] + t->rotation[a][1] * lm.model.vertex[v][1]
                + t->rotation[a][2] * lm.model.vertex[v][2];
            lo[a] = w < lo[a] ? w : lo[a], hi[a] = w > hi[a] ? w : hi[a];
        }
    }
    float radius = 0;
    for (int a = 0; a < 3; a++) {
        view.target[a] = (lo[a] + hi[a]) * 0.5f;
        float h = (hi[a] - lo[a]) * 0.5f;
        radius += h * h;
    }
    radius = sqrtf(radius);
    view.distance = fmaxf(radius * 1.25f / sinf(view.fov * 0.5f * 3.14159265f / 180), 40);
    /* the stage (the in-game studio's: the lathe grows with the camera's reach), the room the renderer's scene capture takes it for */
    pc_stage_cfg stage;
    int actor_light;
    pc_studio_app_stage(&stage.radius, &stage.fillet, &stage.height, &stage.grey, &stage.facet, &stage.floor_step, &stage.wall_step, &actor_light);
    {
        float reach = view.distance * 1.15f, scale = reach / (stage.radius + stage.fillet);
        scale = scale < 1 ? 1 : scale > 12 ? 12 : scale;
        stage.radius *= scale, stage.fillet *= scale, stage.height *= scale;
    }
    unsigned need = 0, stage_length;
    char why[160], info[96];
    pc_stage_build(&stage, 0, 0, 0, NULL, 0, &need, why, sizeof why, info, sizeof info);
    unsigned char* stage_block = calloc(need, 1);
    stage_length = pc_stage_build(&stage, 0, 0, 0, stage_block, need, &need, why, sizeof why, info, sizeof info);
    render_polygon* stage_polygons = NULL;
    int stage_count = stage_length ? pc_stage_polygons(stage_block, stage_length, (void**)&stage_polygons) : -1;
    if (stage_count <= 0) {
        fprintf(stderr, "[rood-studio] no stage: %s\n", why);
        return 1;
    }
    render_mesh stage_mesh = render_mesh_create(stage_polygons, stage_count);
    free(stage_polygons);
    pc_studio_app_room(stage_block, stage_length);
    pc_studio_app_look(look);
    fprintf(stderr, "[rood-studio] %s\n", info);
    int buffer = 0;
    for (int frame = 0; frames <= 0 || frame < frames + 40; frame++) {
        buffer ^= 1;
        pc_frame_begin();
        render_camera cam;
        if (have_ref)
            cam = ref.camera;
        else
            make_camera(&view, &cam);
        { /* the camera the scene capture, the path tracer and the presenter reconstruct the picture with: the GTE's view matrix (libgte MATRIX) */
            struct {
                short m[3][3];
                short pad;
                int t[3];
            } view_matrix;
            for (int a = 0; a < 3; a++) {
                for (int b = 0; b < 3; b++)
                    view_matrix.m[a][b] = (short)lroundf(cam.rotation[a][b] * 4096.0f);
                view_matrix.t[a] = (int)lroundf(cam.translation[a]);
            }
            view_matrix.pad = 0;
            pc_scene_camera(&view_matrix);
        }
        pc_scene_room(stage_block);
        pc_studio_app_look(look); /* (every frame, as the game's studio does with every render: a settings reload gives the room's own values back) */
        {
            render_rules room_rules;
            memset(&room_rules, 0, sizeof room_rules);
            room_rules.near_bucket = 0, room_rules.far_bucket = 1 << 20, room_rules.lod_bucket = 1 << 20;
            uint32_t room = render_list_begin(&cam, &room_rules);
            if (room) {
                render_instance ri;
                memset(&ri, 0, sizeof ri);
                ri.mesh = stage_mesh;
                for (int k = 0; k < 3; k++)
                    ri.rotation[k][k] = 1;
                render_list_add(room, &ri);
                pc_item* it = pc_frame_add(PC_ITEM_NATIVE, 1);
                if (it)
                    it->mode = PC_ITEM_INHERIT, it->native_list = room & 0xffffff;
            }
        }
        render_rules rules;
        memset(&rules, 0, sizeof rules);
        rules.far_bucket = 1 << 20, rules.lod_bucket = 0x7fffffff, rules.objects = 1, rules.zsf3 = 0x155;
        uint32_t list = render_list_begin(&cam, &rules);
        if (list) {
            render_instance in;
            memset(&in, 0, sizeof in);
            in.mesh = lm.mesh;
            in.palette = palette;
            in.bones = lm.bones;
            /* flat models take the model's colour (the room's light at it); coloured ones their vertices' (actor_native.c submit) */
            in.override = RENDER_OVERRIDE_TEXTURE | (lm.model.kind == PC_NM_FLAT ? RENDER_OVERRIDE_COLOR : 0);
            if (have_ref)
                in.override = ref.override_flags;
            unsigned colour = have_ref ? ref.colour : 0x808080;
            in.color[0] = colour & 0xff, in.color[1] = colour >> 8 & 0xff, in.color[2] = colour >> 16 & 0xff;
            in.tpage = (uint16_t)lm.tpage, in.clut = (uint16_t)lm.clut;
            render_list_add(list, &in);
            pc_item* it = pc_frame_add(PC_ITEM_NATIVE, 0);
            if (it)
                it->mode = PC_ITEM_INHERIT, it->native_list = list & 0xffffff;
        }
        put_frame(buffer);
        pc_frame_draw();
        VSync(0);
        if (dump && frame == frames && pc_present_capture)
            snprintf(pc_present_capture, 400, "%s", dump);
    }
    return status;
}

/* ---- the app: the studio's own state (studio.cpp) drives what is drawn, every frame ---- */

typedef struct {
    loaded_model body, weapon, shield, rig;
    int has_weapon, has_shield, has_rig;
    render_transform body_palette[RENDER_MAX_BONES], weapon_palette[RENDER_MAX_BONES], shield_palette[RENDER_MAX_BONES];
    pc_nm_pose pose;
    int kind_weapon; /* a weapon alone: its holder (the rig) is not drawn */
    pc_nm_table common, battle; /* an enemy's sequence tables (parts of its ZUD) */
    pc_nm_anim* anim;
    int anim_id, ids[256], id_count;
} shown_t;

static void unload(shown_t* sh)
{
    if (sh->anim)
        pc_nm_anim_close(sh->anim);
    if (sh->body.mesh || sh->body.model.vertices)
        free_model(&sh->body);
    if (sh->has_weapon)
        free_model(&sh->weapon);
    if (sh->has_shield)
        free_model(&sh->shield);
    if (sh->has_rig)
        free_model(&sh->rig);
    memset(sh, 0, sizeof *sh);
}

/* The model the studio asks for: its parts (an enemy's weapon and shield out of its ZUD, a weapon's rig) and the texture slots they stand in. */
static int load_shown(shown_t* sh, const pc_studio_app_view* v)
{
    memset(sh, 0, sizeof *sh);
    if (!v->has_item)
        return 0;
    if (!strcmp(v->kind, "weapon")) {
        sh->kind_weapon = 1;
        if (load_model(&sh->weapon, "weapon", v->id, 8))
            return -1;
        sh->has_weapon = 1;
        char path[32];
        snprintf(path, sizeof path, "OBJ/%02X.SHP", v->rig);
        unsigned char* file;
        unsigned size;
        char err[160];
        if (!disc(path, &file, &size))
            return -1;
        memset(&sh->rig, 0, sizeof sh->rig);
        int r = pc_nm_shp_decode(file, size, &sh->rig.model, err, sizeof err);
        free(file);
        if (r)
            return -1;
        sh->has_rig = 1;
        pc_nm_pose_rest(&sh->rig.model, &sh->pose);
        return 0;
    }
    if (load_model(&sh->body, v->kind, v->id, 8))
        return -1;
    pc_nm_pose_rest(&sh->body.model, &sh->pose);
    if (sh->body.zud) { /* the ZUD's sequence parts: the common table (animations 1..99) and the battle table (100..) */
        const unsigned char* z = sh->body.zud;
        for (int part = 3; part < 5; part++) {
            const unsigned char* at = z + 8 + 8 * part;
            unsigned offset = (unsigned)(at[0] | at[1] << 8 | at[2] << 16 | (unsigned)at[3] << 24), length = (unsigned)(at[4] | at[5] << 8 | at[6] << 16 | (unsigned)at[7] << 24);
            if (length > 8 && offset + length <= sh->body.zud_size)
                pc_nm_table_from_block(z + offset, length, part == 3 ? &sh->common : &sh->battle);
        }
    }
    if (!strcmp(v->kind, "enemy") && v->scene.equipment && sh->body.zud) {
        const unsigned char* z = sh->body.zud;
        for (int part = 0; part < 2; part++) {
            int wanted = z[part ? 4 : 1];
            const unsigned char* at = z + 8 + 8 * (part + 1);
            unsigned offset = (unsigned)(at[0] | at[1] << 8 | at[2] << 16 | (unsigned)at[3] << 24), length = (unsigned)(at[4] | at[5] << 8 | at[6] << 16 | (unsigned)at[7] << 24);
            if (!wanted || !length || offset + length > sh->body.zud_size)
                continue;
            loaded_model* lm = part ? &sh->shield : &sh->weapon;
            if (load_wep_part(lm, z + offset, length, part ? 8 : 4, part ? 0xF1 : 0xF0, z[part ? 5 : 3]) == 0)
                (part ? &sh->has_shield : &sh->has_weapon)[0] = 1;
        }
    }
    return 0;
}

/* This frame's bones of every part (the facing the turntable turns), and the world box of what is shown. */
static int pose_shown(shown_t* sh, const pc_studio_app_view* v, float lo[3], float hi[3])
{
    /* the sequence: the one asked for, else the game's own choice (animation 1, and the weapon-drawn 101 the studio sets; the model's own table when the
     * model has neither) */
    loaded_model* holder = sh->kind_weapon ? &sh->rig : &sh->body;
    int want = v->animation;
    pc_nm_table table;
    int index;
    if (want < 0 || pc_nm_find_animation(&holder->model, sh->common.bytes ? &sh->common : NULL, sh->battle.bytes ? &sh->battle : NULL, want, &table, &index)) {
        want = 101;
        if (pc_nm_find_animation(&holder->model, sh->common.bytes ? &sh->common : NULL, sh->battle.bytes ? &sh->battle : NULL, want, &table, &index))
            want = 0, pc_nm_find_animation(&holder->model, NULL, NULL, 0, &table, &index);
    }
    if (!sh->anim || sh->anim_id != want) {
        if (sh->anim)
            pc_nm_anim_close(sh->anim);
        sh->anim = pc_nm_anim_open(&holder->model, &table, index);
        sh->anim_id = want;
        sh->id_count = pc_nm_list_animations(&holder->model, sh->common.bytes ? &sh->common : NULL, sh->battle.bytes ? &sh->battle : NULL, sh->ids, 256);
        pc_studio_app_animations(sh->ids, sh->id_count, sh->anim ? pc_nm_anim_frames(sh->anim) : 1, want);
    }
    if (sh->anim) {
        int facing = v->scene.facing;
        pc_nm_anim_holds(sh->anim, v->holds);
        pc_nm_anim_pose(sh->anim, (int)v->animation_time, &sh->pose);
        sh->pose.facing = facing;
    }
    sh->pose.facing = v->scene.facing;
    lo[0] = lo[1] = lo[2] = 1e30f, hi[0] = hi[1] = hi[2] = -1e30f;
    const loaded_model* parts[3] = { NULL, NULL, NULL };
    render_transform* palettes[3] = { NULL, NULL, NULL };
    if (sh->kind_weapon) {
        if (pc_nm_pose_transforms(&sh->rig.model, &sh->pose, sh->body_palette) < 0)
            return -1;
        int hand = pc_nm_find_bone(&sh->rig.model, 0xF0);
        if (hand < 0 || pc_nm_weapon_transforms(&sh->weapon.model, &sh->body_palette[hand], sh->weapon_palette) < 0)
            return -1;
        parts[0] = &sh->weapon, palettes[0] = sh->weapon_palette;
    } else if (sh->body.mesh) {
        if (pc_nm_pose_transforms(&sh->body.model, &sh->pose, sh->body_palette) < 0)
            return -1;
        parts[0] = &sh->body, palettes[0] = sh->body_palette;
        int hand = pc_nm_find_bone(&sh->body.model, 0xF0);
        if (sh->has_weapon && hand >= 0 && pc_nm_weapon_transforms(&sh->weapon.model, &sh->body_palette[hand], sh->weapon_palette) >= 0)
            parts[1] = &sh->weapon, palettes[1] = sh->weapon_palette;
        int off = pc_nm_find_bone(&sh->body.model, 0xF1);
        if (sh->has_shield && off >= 0 && pc_nm_weapon_transforms(&sh->shield.model, &sh->body_palette[off], sh->shield_palette) >= 0)
            parts[2] = &sh->shield, palettes[2] = sh->shield_palette;
    } else
        return 0;
    for (int p = 0; p < 3; p++) {
        if (!parts[p])
            continue;
        const pc_nm_model* m = &parts[p]->model;
        for (int i = 0; i < m->vertices; i++) {
            const render_transform* t = &palettes[p][m->bone_of[i]];
            for (int a = 0; a < 3; a++) {
                float w = t->translation[a] + t->rotation[a][0] * m->vertex[i][0] + t->rotation[a][1] * m->vertex[i][1] + t->rotation[a][2] * m->vertex[i][2];
                lo[a] = w < lo[a] ? w : lo[a], hi[a] = w > hi[a] ? w : hi[a];
            }
        }
    }
    return 1;
}

typedef struct {
    pc_stage_cfg cfg;
    int hidden;
    unsigned char* block;
    unsigned length;
    render_mesh mesh;
} stage_t;

/* The stage the studio's data asks for (it grows with the camera's reach); rebuilt when its measures change. */
static int build_stage_for(stage_t* st, const pc_studio_app_view* v)
{
    pc_stage_cfg cfg;
    cfg.radius = v->scene.radius, cfg.fillet = v->scene.fillet, cfg.height = v->scene.height;
    cfg.grey = v->scene.grey, cfg.facet = v->scene.facet, cfg.floor_step = v->scene.floor_step, cfg.wall_step = v->scene.wall_step;
    int hidden = v->scene.backdrop;
    if (st->mesh && st->hidden == hidden && !memcmp(&st->cfg, &cfg, sizeof cfg))
        return 0;
    if (st->mesh)
        render_mesh_destroy(st->mesh);
    free(st->block);
    char why[160], info[96];
    unsigned need = 0;
    if (hidden) {
        need = 4 + 0x40 + 8 + 32;
        st->block = calloc(need, 1);
        st->length = pc_stage_build_hidden(0, 0, 0, st->block, need, why, sizeof why, info, sizeof info);
    } else {
        pc_stage_build(&cfg, 0, 0, 0, NULL, 0, &need, why, sizeof why, info, sizeof info);
        st->block = calloc(need, 1);
        st->length = pc_stage_build(&cfg, 0, 0, 0, st->block, need, &need, why, sizeof why, info, sizeof info);
    }
    render_polygon* polygons = NULL;
    int count = st->length ? pc_stage_polygons(st->block, st->length, (void**)&polygons) : -1;
    if (count <= 0)
        return -1;
    st->mesh = render_mesh_create(polygons, count);
    free(polygons);
    st->cfg = cfg, st->hidden = hidden;
    pc_studio_app_room(st->block, st->length);
    return 0;
}

static void camera_from(const pc_studio_app_view* v, render_camera* cam)
{
    float f[3] = { cosf(v->pitch) * sinf(v->yaw), -sinf(v->pitch), cosf(v->pitch) * cosf(v->yaw) };
    view_t view = { 0, 0, v->distance, v->fov, { v->target[0], v->target[1], v->target[2] } };
    (void)f;
    /* (make_camera takes degrees: the studio's yaw already has the model's facing in it) */
    view.yaw = v->yaw * 180.0f / 3.14159265f, view.elevation = -v->pitch * 180.0f / 3.14159265f;
    make_camera(&view, cam);
}

static void draw_part(const loaded_model* lm, const render_transform* palette, const render_camera* cam, unsigned colour)
{
    render_rules rules;
    memset(&rules, 0, sizeof rules);
    rules.far_bucket = 1 << 20, rules.lod_bucket = 0x7fffffff, rules.objects = 1, rules.zsf3 = 0x155;
    uint32_t list = render_list_begin(cam, &rules);
    if (!list)
        return;
    render_instance in;
    memset(&in, 0, sizeof in);
    in.mesh = lm->mesh;
    in.palette = palette;
    in.bones = lm->bones;
    in.override = RENDER_OVERRIDE_TEXTURE | (lm->model.kind == PC_NM_FLAT ? RENDER_OVERRIDE_COLOR : 0);
    in.color[0] = colour & 0xff, in.color[1] = colour >> 8 & 0xff, in.color[2] = colour >> 16 & 0xff;
    in.tpage = (uint16_t)lm->tpage, in.clut = (uint16_t)lm->clut;
    render_list_add(list, &in);
    pc_item* it = pc_frame_add(PC_ITEM_NATIVE, 0);
    if (it)
        it->mode = PC_ITEM_INHERIT, it->native_list = list & 0xffffff;
}


static int run_app(int argc, char** argv)
{
    const char *kind = NULL, *dump = NULL, *look = NULL;
    int id = 0, frames = 0, ui = -1;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--character") && i + 1 < argc)
            kind = "character", id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--enemy") && i + 1 < argc)
            kind = "enemy", id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--weapon") && i + 1 < argc)
            kind = "weapon", id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump = argv[++i];
        else if (!strcmp(argv[i], "--look") && i + 1 < argc)
            look = argv[++i];
        else if (!strcmp(argv[i], "--ui"))
            ui = 1;
        else if (!strcmp(argv[i], "--no-ui"))
            ui = 0;
    }
    if (ui < 0)
        ui = dump ? 0 : 1; /* the window shows the studio's panels; a picture asked for is the picture alone */
    screen_init();
    pc_studio_app_begin();
    pc_studio_app_ui(ui);
    if (look)
        pc_studio_app_set_look(look);
    if (kind) {
        char why[160];
        if (pc_studio_app_summon(kind, id, why, sizeof why)) {
            fprintf(stderr, "[rood-studio] %s %d: %s\n", kind, id, why);
            return 1;
        }
    }
    shown_t shown;
    memset(&shown, 0, sizeof shown);
    stage_t stage;
    memset(&stage, 0, sizeof stage);
    int serial = -2, buffer = 0;
    for (int frame = 0; frames <= 0 || frame < frames + 40; frame++) {
        buffer ^= 1;
        pc_studio_app_view v;
        int now = pc_studio_app_frame(&v);
        if (now != serial) {
            unload(&shown);
            serial = now;
            if (load_shown(&shown, &v))
                fprintf(stderr, "[rood-studio] cannot show %s %d\n", v.kind, v.id);
            else if (v.has_item)
                fprintf(stderr, "[rood-studio] %s %d shown\n", v.kind, v.id);
        }
        float lo[3], hi[3];
        unsigned meshes[3];
        unsigned long long keys[3];
        int measured = pose_shown(&shown, &v, lo, hi) > 0, count = 0;
        const loaded_model* drawn[3] = { shown.kind_weapon ? &shown.weapon : shown.body.mesh ? &shown.body : NULL, shown.has_weapon && !shown.kind_weapon ? &shown.weapon : NULL,
            shown.has_shield ? &shown.shield : NULL };
        const render_transform* palettes[3] = { shown.kind_weapon ? shown.weapon_palette : shown.body_palette, shown.weapon_palette, shown.shield_palette };
        for (int p = 0; p < 3; p++)
            if (drawn[p])
                meshes[count] = drawn[p]->mesh, keys[count++] = drawn[p]->key;
        if (measured)
            pc_studio_app_measured(lo, hi, meshes, keys, count);
        if (build_stage_for(&stage, &v)) {
            fprintf(stderr, "[rood-studio] no stage\n");
            return 1;
        }
        pc_frame_begin();
        if (v.scene.backdrop) /* a solid backdrop: a tile of the colour, drawn first (the frame's VRAM holds 5 bits a channel, the tile's colour is the level) */
            pc_frame_tile(3, 0, 0, 320, 224, v.scene.backdrop_rgb & 0xffffff, PC_ITEM_INHERIT, 0);
        render_camera cam;
        camera_from(&v, &cam);
        { /* the camera the scene capture, the path tracer and the presenter reconstruct the picture with: the GTE's view matrix (libgte MATRIX) */
            struct {
                short m[3][3];
                short pad;
                int t[3];
            } view_matrix;
            for (int a = 0; a < 3; a++) {
                for (int b = 0; b < 3; b++)
                    view_matrix.m[a][b] = (short)lroundf(cam.rotation[a][b] * 4096.0f);
                view_matrix.t[a] = (int)lroundf(cam.translation[a]);
            }
            view_matrix.pad = 0;
            pc_scene_camera(&view_matrix);
        }
        pc_scene_room(stage.block);
        {
            render_rules room_rules;
            memset(&room_rules, 0, sizeof room_rules);
            room_rules.near_bucket = 0, room_rules.far_bucket = 1 << 20, room_rules.lod_bucket = 1 << 20;
            uint32_t room = render_list_begin(&cam, &room_rules);
            if (room) {
                render_instance ri;
                memset(&ri, 0, sizeof ri);
                ri.mesh = stage.mesh;
                for (int k = 0; k < 3; k++)
                    ri.rotation[k][k] = 1;
                render_list_add(room, &ri);
                pc_item* it = pc_frame_add(PC_ITEM_NATIVE, 1);
                if (it)
                    it->mode = PC_ITEM_INHERIT, it->native_list = room & 0xffffff;
            }
        }
        if (measured)
            for (int p = 0; p < 3; p++)
                if (drawn[p])
                    draw_part(drawn[p], palettes[p], &cam, (unsigned)v.scene.actor_light & 0xffffff);
        background = v.scene.backdrop ? ((v.scene.backdrop_rgb & 0xff) + (v.scene.backdrop_rgb >> 8 & 0xff) + (v.scene.backdrop_rgb >> 16 & 0xff)) / 3 : 30;
        put_frame(buffer);
        unsigned presented = &pc_presents ? pc_presents : 0;
        pc_frame_draw();
        VSync(0);
        /* A picture asked for does not depend on the machine: every frame is shown before the next is made (the lighting's history, the temporal filters
         * and the exposure follow the frames, not how many images a busy machine got to show). */
        if (dump && &pc_presents)
            for (int spins = 0; pc_presents == presented && spins < 20000; spins++)
                usleep(250);
        pc_studio_app_landed();
        if (dump && frame == frames && pc_present_capture)
            snprintf(pc_present_capture, 400, "%s", dump);
    }
    return 0;
}

int pc_rood_studio_run(int argc, char** argv)
{
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--reference") || !strcmp(argv[i], "--check-catalog"))
            return run_fixed(argc, argv);
    return run_app(argc, argv);
}
