/* Native model decoders: see native_model.h. Pure data: no game headers, no guest memory. */
#include "native_model.h"
#include "../model_key.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned u32_at(const uint8_t* p)
{
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}
static unsigned u16_at(const uint8_t* p) { return p[0] | (unsigned)p[1] << 8; }
static int fail(char* err, size_t size, const char* what)
{
    if (err && size)
        snprintf(err, size, "%s", what);
    return -1;
}

/* The records' size in words, walked as actor_native.c record_words does (0: the records disagree with their kind). */
static unsigned walk_records(const uint32_t* r, const int counts[3], int kind, unsigned limit)
{
    unsigned at = 0;
    for (int section = 0; section < 3; section++)
        for (int face = 0; face < counts[section]; face++) {
            int quad;
            unsigned words;
            if (kind == PC_NM_FLAT) {
                if (at + 1 > limit)
                    return 0;
                quad = section == 1 || (section == 2 && (r[at] & 255) == 0x2c);
                words = quad ? 5 : 4;
                if (section == 2 && ((r[at] >> 8) & 255) != words * 4)
                    return 0;
            } else {
                if (at + 4 > limit)
                    return 0;
                quad = section == 1 || (section == 2 && (r[at + 2] >> 24) == 0x3c);
                words = quad ? 8 : 6;
                if (section == 2 && (r[at + 3] >> 24) != words * 4)
                    return 0;
            }
            at += words;
            if (at > limit)
                return 0;
        }
    return at;
}

/* The model block (an SHP's model, a WEP's offsets block) into `out`. */
static int decode_model(const uint8_t* model, size_t size, int bones, const uint8_t head[8], int armatures, pc_nm_model* out, char* err,
    size_t err_size)
{
    if (size < 0x40)
        return fail(err, err_size, "model block too small");
    memcpy(out->object_head, head, 8);
    memcpy(out->model_head, model, sizeof out->model_head);
    out->bones = bones;
    out->groups = head[1];
    out->counts[0] = (int)u16_at(head + 2), out->counts[1] = (int)u16_at(head + 4), out->counts[2] = (int)u16_at(head + 6);
    unsigned groups_at = u32_at(model + 0x34), vertices_at = u32_at(model + 0x38), records_at = u32_at(model + 0x3c);
    if (!out->groups || groups_at > size || groups_at + 4u * (unsigned)out->groups > size)
        return fail(err, err_size, "vertex groups outside the model");
    out->group_words = malloc(4u * (unsigned)out->groups);
    memcpy(out->group_words, model + groups_at, 4u * (unsigned)out->groups);
    out->vertices = (int)(out->group_words[out->groups - 1] >> 16);
    if (!out->vertices || out->vertices > 768 || vertices_at > size || vertices_at + 8u * (unsigned)out->vertices > size)
        return fail(err, err_size, "vertices outside the model");
    out->vertex = malloc(8u * (unsigned)out->vertices);
    memcpy(out->vertex, model + vertices_at, 8u * (unsigned)out->vertices);
    out->bone_of = malloc((size_t)out->vertices);
    unsigned cursor = 0;
    for (int g = 0; g < out->groups; g++) {
        unsigned end = out->group_words[g] >> 16, bone = out->group_words[g] & 0xffff;
        if (end < cursor || end > (unsigned)out->vertices || bone >= RENDER_MAX_BONES)
            return fail(err, err_size, "vertex groups out of order");
        for (; cursor < end; cursor++)
            out->bone_of[cursor] = (uint8_t)bone;
    }
    if (records_at > size)
        return fail(err, err_size, "records outside the model");
    unsigned room = (unsigned)((size - records_at) / 4);
    uint32_t* words = malloc((room ? room : 1) * 4u);
    memcpy(words, model + records_at, (size_t)room * 4);
    /* the layout: the loader takes it from the texture block's flags (a shared texture: from the model it shares); here the layout
     * whose records all name vertices of the model (flat records are offsets in their words 1 and 2, coloured ones in words 0 and 1) */
    int kind = -1;
    for (int k = 0; k < 2 && kind < 0; k++) {
        unsigned used = walk_records(words, out->counts, k, room);
        if (!used)
            continue;
        int valid = 1;
        unsigned at = 0;
        for (int section = 0; section < 3 && valid; section++)
            for (int face = 0; face < out->counts[section] && valid; face++) {
                int quad;
                unsigned w, offs[4];
                if (k == 0) {
                    quad = section == 1 || (section == 2 && (words[at] & 255) == 0x2c);
                    w = quad ? 5 : 4;
                    offs[0] = words[at + 1] & 0xffff, offs[1] = words[at + 1] >> 16, offs[2] = words[at + 2] & 0xffff, offs[3] = words[at + 2] >> 16;
                } else {
                    quad = section == 1 || (section == 2 && (words[at + 2] >> 24) == 0x3c);
                    w = quad ? 8 : 6;
                    offs[0] = words[at] & 0xffff, offs[1] = words[at] >> 16, offs[2] = words[at + 1] & 0xffff, offs[3] = words[at + 1] >> 16;
                }
                for (int c = 0; c < (quad ? 4 : 3); c++)
                    if ((offs[c] & 3) || (offs[c] >> 2) >= (unsigned)out->vertices)
                        valid = 0;
                at += w;
            }
        if (valid)
            kind = k, out->record_words = used;
    }
    if (kind < 0) {
        free(words);
        return fail(err, err_size, "records fit neither the flat nor the coloured layout");
    }
    out->kind = kind;
    out->records = words;
    if (armatures) {
        if (0x40 + 16u * (unsigned)bones > size)
            return fail(err, err_size, "armatures outside the model");
        out->bone = calloc((size_t)bones, sizeof *out->bone);
        for (int i = 0; i < bones; i++) {
            const uint8_t* a = model + 0x40 + 16 * i;
            memcpy(out->bone[i].raw, a, 16);
            out->bone[i].length = (int16_t)u16_at(a); /* (the game reads it as a u_short and stores the negation in a short: signed) */
            out->bone[i].parent = a[4];
            out->bone[i].kind = a[8];
            out->bone[i].lock = a[12];
        }
    }
    return 0;
}

int pc_nm_shp_decode(const uint8_t* file, size_t size, pc_nm_model* out, char* err, size_t err_size)
{
    memset(out, 0, sizeof *out);
    out->shared_model = -1;
    if (size < 0x100)
        return fail(err, err_size, "SHP too small");
    int model_size = (int)u32_at(file + 0xF4);
    if (model_size < 0x40 || (size_t)model_size + 0xF8 + 8 > size)
        return fail(err, err_size, "SHP model size");
    const uint8_t* model = file + 0xF8;
    if (decode_model(model, (size_t)model_size, file[4], file + 4, 1, out, err, err_size)) {
        pc_nm_free(out);
        return -1;
    }
    memcpy(out->overlays, file + 0xC, sizeof out->overlays);
    const uint8_t* seq = model + model_size;
    unsigned seq_length = u32_at(seq + 4);
    if ((size_t)(seq - file) + 8 + seq_length + 4 > size) {
        pc_nm_free(out);
        return fail(err, err_size, "SHP sequence block");
    }
    out->sequence = malloc(seq_length ? seq_length : 1);
    memcpy(out->sequence, seq + 8, seq_length);
    out->sequence_size = seq_length;
    out->sequence_head = seq[0];
    const uint8_t* block = seq + 8 + seq_length;
    unsigned block_size = u32_at(block);
    const uint8_t* body = block + 4;
    size_t left = size - (size_t)(body - file);
    if (block_size == 4) {
        out->shared_model = (int)u32_at(body);
        return 0;
    }
    if (left < 4) {
        pc_nm_free(out);
        return fail(err, err_size, "SHP texture block");
    }
    out->eight_bit = body[0] & 1;
    out->texture_width = body[1] * 2;
    out->texture_height = body[2] * 2;
    out->colours = body[3];
    size_t palette_bytes = (size_t)out->colours * 4, texel_bytes = (size_t)out->texture_width * (size_t)out->texture_height;
    if (4 + palette_bytes + texel_bytes > left) {
        pc_nm_free(out);
        return fail(err, err_size, "SHP texture past the file");
    }
    out->palette = malloc(palette_bytes ? palette_bytes : 2);
    memcpy(out->palette, body + 4, palette_bytes);
    out->texels = malloc(texel_bytes ? texel_bytes : 1);
    memcpy(out->texels, body + 4 + palette_bytes, texel_bytes);
    out->has_texture = 1;
    return 0;
}

int pc_nm_wep_decode(const uint8_t* file, size_t size, pc_nm_model* out, char* err, size_t err_size)
{
    memset(out, 0, sizeof *out);
    out->shared_model = -1;
    out->wep = 1;
    if (size < 0x40)
        return fail(err, err_size, "WEP too small");
    unsigned offsets_size = u32_at(file + 0xC);
    if (offsets_size < 0x40 || 0x10 + (size_t)offsets_size + 8 > size)
        return fail(err, err_size, "WEP offsets size");
    if (decode_model(file + 0x10, offsets_size, file[4], file + 4, 1, out, err, err_size)) {
        pc_nm_free(out);
        return -1;
    }
    const uint8_t* after = file + 0x10 + offsets_size;
    out->texture_width = (int)(after[5] * 2u);
    out->texture_height = 32;
    out->eight_bit = 1;
    int double_palette = after[7] == 96;
    out->wep_first_palette_bytes = double_palette ? 64 : 32;
    out->wep_palette_bytes = double_palette ? 128 : 64;
    out->palettes_wep = 7;
    size_t need = 8 + out->wep_first_palette_bytes + 7 * (size_t)out->wep_palette_bytes + (size_t)out->texture_width * 32 + 8u * (unsigned)out->bones;
    if ((size_t)(after - file) + need > size) {
        pc_nm_free(out);
        return fail(err, err_size, "WEP palettes and texture past the file");
    }
    out->colours = (int)(out->wep_first_palette_bytes / 2);
    out->palette = malloc(out->wep_first_palette_bytes);
    memcpy(out->palette, after + 8, out->wep_first_palette_bytes);
    out->wep_palettes = malloc(7 * (size_t)out->wep_palette_bytes);
    memcpy(out->wep_palettes, after + 8 + out->wep_first_palette_bytes, 7 * (size_t)out->wep_palette_bytes);
    const uint8_t* texture = after + 8 + out->wep_first_palette_bytes + 7 * out->wep_palette_bytes;
    out->texels = malloc((size_t)out->texture_width * 32);
    memcpy(out->texels, texture, (size_t)out->texture_width * 32);
    out->bone_info = malloc(8u * (unsigned)(out->bones ? out->bones : 1));
    memcpy(out->bone_info, texture + (size_t)out->texture_width * 32, 8u * (unsigned)out->bones);
    out->has_texture = 1;
    return 0;
}

void pc_nm_free(pc_nm_model* m)
{
    free(m->group_words), free(m->vertex), free(m->bone_of), free(m->records), free(m->bone);
    free(m->palette), free(m->texels), free(m->sequence), free(m->bone_info), free(m->wep_palettes);
    memset(m, 0, sizeof *m);
}

/* (declared before use below) */
static uint32_t* fixed_records(const pc_nm_model* m, int u_add, int v_add);
uint64_t pc_nm_content_key(const pc_nm_model* m, int u_add, int v_add)
{
    unsigned short counts[3] = { (unsigned short)m->counts[0], (unsigned short)m->counts[1], (unsigned short)m->counts[2] };
    uint32_t* records = fixed_records(m, u_add, v_add);
    uint64_t key = pc_model_content_key(m->kind + (m->grouped ? 2 : 0), m->object_head, m->group_words, (unsigned)m->groups, &m->vertex[0][0],
        (unsigned)m->vertices, m->model_head, records, counts);
    free(records);
    return key;
}

/* The records with a texture slot's rows added to every texture coordinate (the loaders' fix-up). */
static uint32_t* fixed_records(const pc_nm_model* m, int u_add, int v_add)
{
    uint32_t* r = malloc((m->record_words ? m->record_words : 1) * 4u);
    memcpy(r, m->records, (size_t)m->record_words * 4);
    unsigned at = 0;
    for (int section = 0; section < 3; section++)
        for (int face = 0; face < m->counts[section]; face++) {
            int quad;
            unsigned words;
            int lane_word[4], lane_half[4], lanes;
            if (m->kind == PC_NM_FLAT) {
                quad = section == 1 || (section == 2 && (r[at] & 255) == 0x2c);
                words = quad ? 5 : 4;
                lane_word[0] = 3, lane_half[0] = 0, lane_word[1] = 3, lane_half[1] = 1;
                if (quad)
                    lane_word[2] = 4, lane_half[2] = 0, lane_word[3] = 4, lane_half[3] = 1, lanes = 4;
                else
                    lane_word[2] = 2, lane_half[2] = 1, lanes = 3;
            } else {
                quad = section == 1 || (section == 2 && (r[at + 2] >> 24) == 0x3c);
                words = quad ? 8 : 6;
                int base = quad ? 6 : 5;
                lane_word[0] = base, lane_half[0] = 0, lane_word[1] = base, lane_half[1] = 1;
                if (quad)
                    lane_word[2] = 7, lane_half[2] = 0, lane_word[3] = 7, lane_half[3] = 1, lanes = 4;
                else
                    lane_word[2] = 1, lane_half[2] = 1, lanes = 3;
            }
            for (int k = 0; k < lanes; k++) {
                unsigned shift = lane_half[k] ? 16 : 0;
                uint32_t* w = &r[at + (unsigned)lane_word[k]];
                unsigned lane = (*w >> shift) & 0xffff;
                unsigned u = ((lane & 255) + (unsigned)u_add) & 255, v = ((lane >> 8) + (unsigned)v_add) & 255;
                *w = (*w & ~(0xffffu << shift)) | ((u | v << 8) << shift);
            }
            at += words;
        }
    return r;
}

unsigned pc_nm_mesh_hash(const pc_nm_model* m, int u_add, int v_add)
{
    uint32_t* r = fixed_records(m, u_add, v_add);
    unsigned hash = (2166136261u ^ (unsigned)(m->kind + (m->grouped ? 2 : 0))) * 16777619u;
    const unsigned* head = (const unsigned*)(const void*)m->object_head;
    for (int i = 0; i < 2; i++)
        hash = (hash ^ head[i]) * 16777619u;
    for (int i = 0; i < m->groups; i++)
        hash = (hash ^ m->group_words[i]) * 16777619u;
    const unsigned* v = (const unsigned*)(const void*)m->vertex;
    for (int i = 0; i < m->vertices * 2; i++)
        hash = (hash ^ v[i]) * 16777619u;
    for (unsigned i = 0; i < m->record_words; i++)
        hash = (hash ^ r[i]) * 16777619u;
    if (m->grouped) {
        hash = (hash ^ m->group_mask) * 16777619u;
        for (int i = 0; i < 12; i++)
            hash = (hash ^ m->model_head[i]) * 16777619u;
    }
    free(r);
    return hash;
}

int pc_nm_polygons(const pc_nm_model* m, int u_add, int v_add, render_polygon** out)
{
    unsigned total = (unsigned)(m->counts[0] + m->counts[1] + m->counts[2]);
    render_polygon* polygons = calloc(total ? total : 1, sizeof *polygons);
    uint32_t* records = fixed_records(m, u_add, v_add);
    const unsigned* r = records;
    int count = 0;
    for (int section = 0; section < 3; section++)
        for (int face = 0; face < m->counts[section]; face++) {
            render_polygon* p = &polygons[count];
            unsigned offsets[4], uv[4], colours[4] = { 0, 0, 0, 0 }, flags = 0, code;
            int quad, split = 0;
            if (m->kind == PC_NM_FLAT) {
                quad = section == 1 || (section == 2 && (r[0] & 255) == 0x2c);
                if (section == 2)
                    flags = r[0] >> 16;
                offsets[0] = r[1] & 0xffff, offsets[1] = r[1] >> 16, offsets[2] = r[2] & 0xffff, offsets[3] = r[2] >> 16;
                uv[0] = r[3] & 0xffff, uv[1] = r[3] >> 16, uv[2] = quad ? r[4] & 0xffff : r[2] >> 16, uv[3] = quad ? r[4] >> 16 : 0;
                split = section == 2 && (flags & 3) == 3;
                if (split && !quad)
                    goto bad;
                code = quad ? 0x2c : 0x24;
                for (int k = 0; k < 4; k++)
                    colours[k] = 0x808080;
                r += quad ? 5 : 4;
            } else {
                quad = section == 1 || (section == 2 && (r[2] >> 24) == 0x3c);
                if (section == 2)
                    flags = r[4] >> 24;
                offsets[0] = r[0] & 0xffff, offsets[1] = r[0] >> 16, offsets[2] = r[1] & 0xffff, offsets[3] = r[1] >> 16;
                for (int k = 0; k < (quad ? 4 : 3); k++)
                    colours[k] = r[2 + k];
                unsigned uv01 = r[quad ? 6 : 5], uv23 = quad ? r[7] : r[1] >> 16;
                uv[0] = uv01 & 0xffff, uv[1] = uv01 >> 16, uv[2] = uv23 & 0xffff, uv[3] = uv23 >> 16;
                code = r[2] >> 24;
                r += quad ? 8 : 6;
            }
            unsigned page_bits = flags & 0x60;
            if (flags & 16)
                code |= 2;
            for (int k = 0; k < (quad ? 4 : 3); k++) {
                unsigned offset = offsets[k];
                if ((offset & 3) || (offset >> 2) >= (unsigned)m->vertices)
                    goto bad;
                const int16_t* xyz = m->vertex[offset >> 2];
                p->position[k][0] = xyz[0], p->position[k][1] = xyz[1], p->position[k][2] = xyz[2];
                p->bone[k] = m->bone_of[offset >> 2];
                p->uv[k][0] = (uint8_t)(uv[k] & 0xff), p->uv[k][1] = (uint8_t)((uv[k] >> 8) & 0xff);
                p->rgb[k][0] = (uint8_t)(colours[k] & 0xff), p->rgb[k][1] = (uint8_t)((colours[k] >> 8) & 0xff);
                p->rgb[k][2] = (uint8_t)((colours[k] >> 16) & 0xff);
            }
            p->tpage = (uint16_t)page_bits;
            p->code = (uint8_t)code;
            p->flags = (uint8_t)((quad ? RENDER_POLY_QUAD : 0) | (split ? RENDER_POLY_SPLIT : 0) | (!split && (flags & 1) ? RENDER_POLY_DOUBLE_SIDED : 0));
            p->bias = (uint8_t)(split ? 4 : section == 2 ? ((flags >> 2) & 3) * 4 : 0);
            ++count;
        }
    free(records);
    *out = polygons;
    return count;
bad:
    free(records);
    free(polygons);
    return -1;
}

void pc_nm_euler(int ax, int ay, int az, float r[3][3])
{
    const float k = 6.28318530718f / 4096.0f;
    float sx = sinf((float)(ax & 0xfff) * k), cx = cosf((float)(ax & 0xfff) * k);
    float sy = sinf((float)(ay & 0xfff) * k), cy = cosf((float)(ay & 0xfff) * k);
    float sz = sinf((float)(az & 0xfff) * k), cz = cosf((float)(az & 0xfff) * k);
    r[0][0] = cz * cy, r[0][1] = cz * sy * sx - sz * cx, r[0][2] = cz * sy * cx + sz * sx;
    r[1][0] = sz * cy, r[1][1] = sz * sy * sx + cz * cx, r[1][2] = sz * sy * cx - cz * sx;
    r[2][0] = -sy, r[2][1] = cy * sx, r[2][2] = cy * cx;
}

void pc_nm_pose_zero(const pc_nm_model* m, pc_nm_pose* pose)
{
    (void)m;
    memset(pose, 0, sizeof *pose);
    pose->actor_scale = 1820.0f / 4096.0f;
    for (int i = 0; i < RENDER_MAX_BONES; i++)
        pose->scale[i][0] = pose->scale[i][1] = pose->scale[i][2] = 64;
}

static unsigned be16(const uint8_t* p) { return (unsigned)p[0] << 8 | p[1]; }

int pc_nm_pose_transforms(const pc_nm_model* m, const pc_nm_pose* pose, render_transform* out)
{
    if (!m->bone || m->bones <= 0 || m->bones > RENDER_MAX_BONES)
        return -1;
    float unscaled[RENDER_MAX_BONES][3][3]; /* what the children turn from (D_800F2458.boneMatrices) */
    int scale[RENDER_MAX_BONES][3];         /* the chain's scale, 64 = 1 */
    pc_nm_euler(pose->rot[0][0] + pose->rotation_x, pose->rot[0][1] + pose->facing, pose->rot[0][2] + pose->rotation_z, unscaled[0]);
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            unscaled[0][a][b] *= pose->actor_scale;
    for (int k = 0; k < 3; k++)
        scale[0][k] = pose->scale[0][k];
    for (int a = 0; a < 3; a++) {
        out[0].translation[a] = pose->position[a] + (float)pose->offset[a];
        for (int b = 0; b < 3; b++)
            out[0].rotation[a][b] = unscaled[0][a][b] * (float)scale[0][b] / 64.0f;
    }
    for (int i = 1; i < m->bones; i++) {
        int parent = m->bone[i].parent;
        if (parent >= i)
            return -1;
        if (pose->locked[i]) { /* held: where it was, whatever its parents do */
            memcpy(unscaled[i], pose->lock_rotation[i], sizeof unscaled[i]);
            memcpy(out[i].rotation, pose->lock_rotation[i], sizeof out[i].rotation);
            for (int a = 0; a < 3; a++)
                out[i].translation[a] = pose->lock_place[i][a] + pose->position[a];
            for (int k = 0; k < 3; k++)
                scale[i][k] = 64;
            continue;
        }
        float own[3][3];
        pc_nm_euler(pose->rot[i][0], pose->rot[i][1], pose->rot[i][2], own);
        float length = (float)m->bone[parent].length;
        const render_transform* p = &out[parent];
        for (int a = 0; a < 3; a++) {
            out[i].translation[a] = p->translation[a] - p->rotation[a][0] * length;
            for (int b = 0; b < 3; b++)
                unscaled[i][a][b] = unscaled[parent][a][0] * own[0][b] + unscaled[parent][a][1] * own[1][b] + unscaled[parent][a][2] * own[2][b];
        }
        int keys_unit = pose->scale[i][0] == 64 && pose->scale[i][1] == 64 && pose->scale[i][2] == 64;
        for (int k = 0; k < 3; k++)
            scale[i][k] = keys_unit ? scale[parent][k] : scale[parent][k] * pose->scale[i][k] / 64;
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++)
                out[i].rotation[a][b] = unscaled[i][a][b] * (float)scale[i][b] / 64.0f;
    }
    return m->bones;
}

int pc_nm_find_bone(const pc_nm_model* m, int code)
{
    for (int i = 0; m->bone && i < m->bones; i++)
        if (m->bone[i].raw[6] == code)
            return i;
    return -1;
}

int pc_nm_weapon_transforms(const pc_nm_model* w, const render_transform* hand, render_transform* out)
{
    if (!w->wep || !w->bone || !w->bone_info || w->bones <= 0 || w->bones > RENDER_MAX_BONES)
        return -1;
    out[0] = *hand;
    float rot[RENDER_MAX_BONES][3][3]; /* what the children turn from: the hand's matrix, then each bone's */
    memcpy(rot[0], hand->rotation, sizeof rot[0]);
    for (int i = 1; i < w->bones; i++) {
        int parent = w->bone[i].parent;
        if (parent >= i)
            return -1;
        const uint8_t* b = w->bone_info + 8 * i;
        float own[3][3];
        pc_nm_euler((int16_t)u16_at(b), (int16_t)u16_at(b + 2), (int16_t)u16_at(b + 4), own);
        float length = (float)w->bone[parent].length;
        for (int a = 0; a < 3; a++) {
            out[i].translation[a] = out[parent].translation[a] - rot[parent][a][0] * length;
            for (int c = 0; c < 3; c++)
                rot[i][a][c] = rot[parent][a][0] * own[0][c] + rot[parent][a][1] * own[1][c] + rot[parent][a][2] * own[2][c];
        }
        memcpy(out[i].rotation, rot[i], sizeof rot[i]);
    }
    return w->bones;
}

/* ---- sequences ---- */

int pc_nm_table_from_block(const uint8_t* block, size_t size, pc_nm_table* out)
{
    if (size < 8)
        return -1;
    unsigned length = u32_at(block + 4);
    if (length > size - 8)
        length = (unsigned)(size - 8);
    out->bytes = block + 8, out->size = length, out->count = block[0], out->bones = (int)u16_at(block + 2);
    return length >= 8 ? 0 : -1;
}

static int base_table(const pc_nm_model* m, pc_nm_table* t)
{
    if (!m->sequence)
        return -1;
    t->bytes = m->sequence, t->size = m->sequence_size, t->count = m->sequence_head, t->bones = 0;
    return 0;
}

int pc_nm_find_animation(const pc_nm_model* m, const pc_nm_table* common, const pc_nm_table* battle, int id, pc_nm_table* table, int* index)
{
    const pc_nm_table* t = NULL;
    pc_nm_table own;
    int idx;
    if (id == 0) {
        if (base_table(m, &own))
            return -1;
        t = &own, idx = 0;
    } else if (id < 100) {
        t = common, idx = id;
    } else if (id < 0xCA) {
        t = battle, idx = id - 100;
    } else
        return -1;
    if (!t || !t->bytes || (id && idx >= t->count) || t->size < 8 || (t->bones && t->bones != m->bones))
        return -1;
    unsigned index_at = u32_at(t->bytes + 4);
    if (index_at + (unsigned)idx >= t->size)
        return -1;
    int record = t->bytes[index_at + (unsigned)idx];
    if (record == 0xff)
        return -1;
    unsigned stride = (unsigned)m->bones * 4 + 10;
    if (8 + stride * (unsigned)(record + 1) > t->size)
        return -1;
    *table = *t;
    *index = record;
    return 0;
}

int pc_nm_list_animations(const pc_nm_model* m, const pc_nm_table* common, const pc_nm_table* battle, int* ids, int max)
{
    int n = 0;
    for (int id = 0; id < 0xCA && n < max; id++) {
        pc_nm_table t;
        int idx;
        if (id != 0 && id < 100 && (!common || id >= common->count))
            continue;
        if (id >= 100 && (!battle || id - 100 >= battle->count))
            continue;
        if (pc_nm_find_animation(m, common, battle, id, &t, &idx) == 0)
            ids[n++] = id;
    }
    return n;
}

/* One track: the first vector and the packets of frame deltas after it (vs_actors_decodeAnimationTrack, a frame at a time). */
typedef struct {
    const uint8_t* stream; /* the track's bytes to the end of the table */
    unsigned length;
    unsigned pos;
    int next;              /* the frame the current delta lasts to */
    int16_t delta[3], val[3];
    int live;              /* it has packets to read */
    int ended;
} track_t;

static int s16_at(const track_t* t, unsigned at) { return at + 1 < t->length ? (int)(int8_t)t->stream[at] << 8 | t->stream[at + 1] : 0; }

/* Apply frame `frame`'s step: read packets until the delta covers it, then add it (`rotation`: the turns keep 13 bits). */
static void track_step(track_t* t, int frame, int rotation)
{
    if (!t->live || t->ended)
        return;
    while (t->next < frame) {
        if (t->pos >= t->length) {
            t->ended = 1;
            return;
        }
        int flags = t->stream[t->pos];
        if (flags == 0) {
            t->ended = 1;
            return;
        }
        int count, packed;
        if (flags & 0xE0) {
            count = flags & 0x1F;
            if (count == 0x1F)
                t->next += 0x20 + (t->pos + 1 < t->length ? t->stream[t->pos + 1] : 0), t->pos += 2;
            else
                t->next += count + 1, t->pos += 1;
        } else {
            count = flags & 3;
            if (count == 3)
                t->next += 4 + (t->pos + 1 < t->length ? t->stream[t->pos + 1] : 0), t->pos += 2;
            else
                t->next += count + 1, t->pos += 1;
            flags <<= 3;
            count = s16_at(t, t->pos);
            t->pos += 2;
            packed = count;
            count >>= 3;
            int skip_z = 0;
            if (packed & 4) {
                t->delta[0] = (int16_t)count;
                flags &= 0x60;
                if (packed & 3)
                    count = s16_at(t, t->pos), t->pos += 2;
            }
            if (packed & 2) {
                t->delta[1] = (int16_t)count;
                flags &= 0xA0;
                if (packed & 1)
                    count = s16_at(t, t->pos), t->pos += 2;
                else
                    skip_z = 1;
            }
            if (!skip_z && (packed & 1)) {
                t->delta[2] = (int16_t)count;
                flags &= 0xC0;
            }
        }
        for (int k = 0; k < 3; k++)
            if (flags & (0x80 >> k)) {
                t->delta[k] = (int8_t)(t->pos < t->length ? t->stream[t->pos] : 0);
                t->pos += 1;
            }
    }
    for (int k = 0; k < 3; k++) {
        t->val[k] = (int16_t)(t->val[k] + t->delta[k]);
        if (rotation)
            t->val[k] &= 0x1FFF;
    }
}

struct pc_nm_anim {
    const pc_nm_model* m;
    const uint8_t *table, *header, *initial;
    unsigned size, data_at;
    int bones, frames, flags, time;
    track_t bone[RENDER_MAX_BONES], scale[RENDER_MAX_BONES], root;
    int scale_live[RENDER_MAX_BONES];
    uint8_t scale_plain[RENDER_MAX_BONES][3];
    pc_nm_table source;
    int record, no_locks;
    struct {
        int frame, bone, hold;
    } hold[64]; /* the animation's pose-lock events, in order */
    int holds;
};

static void track_open(track_t* t, const pc_nm_anim* a, unsigned own_offset, unsigned initial_offset, int start, int live, int halve)
{
    memset(t, 0, sizeof *t);
    unsigned at = a->data_at + own_offset;
    t->stream = at < a->size ? a->table + at : a->table + a->size;
    t->length = at < a->size ? a->size - at : 0;
    t->pos = (unsigned)start;
    t->live = live;
    unsigned from = a->data_at + initial_offset;
    if (from + 6 <= a->size)
        for (int k = 0; k < 3; k++) {
            int v = (int)be16(a->table + from + 2 * (unsigned)k);
            t->val[k] = (int16_t)(halve ? v : v * 2);
        }
}

static void anim_reset(pc_nm_anim* a)
{
    int keyed = a->header[2] == 0xff;
    int n = a->bones;
    a->time = 0;
    track_open(&a->root, a, u16_at(a->header + 6), u16_at(a->initial + 6), keyed ? 6 : 0, 1, 0);
    for (int i = 0; i < n; i++)
        track_open(&a->bone[i], a, u16_at(a->header + 10 + 2 * i), u16_at(a->initial + 10 + 2 * i), keyed ? 6 : 0, 1, 0);
    for (int i = 0; i < n; i++) {
        memset(&a->scale[i], 0, sizeof a->scale[i]);
        a->scale_live[i] = 0;
        a->scale_plain[i][0] = a->scale_plain[i][1] = a->scale_plain[i][2] = 64;
        if (!(a->flags & 1))
            continue;
        unsigned own = u16_at(a->header + 10 + 2 * (n + i)), first = u16_at(a->initial + 10 + 2 * (n + i));
        const uint8_t* d = a->table + a->data_at + first;
        if (a->data_at + first + 8 > a->size)
            continue;
        if (d[0] == 0xfe && d[1] == 1) {
            /* the keys as a vector after two marker bytes: big-endian, the value itself */
            track_open(&a->scale[i], a, own, first + 2, keyed ? 8 : 0, (a->flags & 2) != 0, 1);
            a->scale_live[i] = 1;
        } else
            a->scale_plain[i][0] = d[0], a->scale_plain[i][1] = d[1], a->scale_plain[i][2] = d[2];
    }
}

/* The animation event script: how many bytes each command takes (vs_actors_animationEventLengths, the game's table; the commands themselves are the game's
 * business: sounds, effects, texture changes. Here only the bone holds, 56 releasing and 57 holding bone `arg`, are read). */
static const uint8_t event_length[65] = { 1, 1, 1, 1, 2, 3, 2, 2, 3, 2, 2, 1, 2, 1, 2, 2, 0, 0, 0, 2, 2, 2, 3, 1, 1, 1, 2, 2, 2, 1, 5, 5, 1, 1, 1, 1, 3, 3, 3, 5, 4, 5, 5, 0, 4, 0, 0, 3, 3, 3, 5, 4, 4, 6, 4, 2, 2, 2, 1, 1, 2, 1, 1, 1, 1 };

static void read_holds(pc_nm_anim* a)
{
    unsigned at = a->data_at + u16_at(a->header + 4);
    if (!u16_at(a->header + 4) || at >= a->size)
        return;
    int frame;
    if (a->table[at] == 0xff) {
        frame = a->table[at + 1] + 0xff;
        at += 2;
    } else
        frame = a->table[at++];
    for (int n = 0; n < 400 && at < a->size; n++) {
        int op = a->table[at];
        if (op == 0 || op >= (int)sizeof event_length || event_length[op] == 0)
            break;
        if ((op == 56 || op == 57) && a->holds < 64 && at + 1 < a->size) {
            a->hold[a->holds].frame = frame, a->hold[a->holds].bone = a->table[at + 1], a->hold[a->holds].hold = op == 57;
            a->holds++;
        }
        at += event_length[op];
        if (at >= a->size)
            break;
        if (a->table[at] == 0xff) {
            frame = a->table[at + 1] + 0xff;
            at += 2;
        } else
            frame = a->table[at++];
    }
}

pc_nm_anim* pc_nm_anim_open(const pc_nm_model* m, const pc_nm_table* table, int index)
{
    unsigned stride = (unsigned)m->bones * 4 + 10, at = 8 + stride * (unsigned)index;
    if (!m->bone || m->bones > RENDER_MAX_BONES || at + stride > table->size || table->size < 8)
        return NULL;
    pc_nm_anim* a = calloc(1, sizeof *a);
    a->m = m, a->table = table->bytes, a->size = table->size, a->data_at = u32_at(table->bytes);
    a->bones = m->bones, a->header = table->bytes + at, a->initial = a->header;
    a->frames = (int)u16_at(a->header);
    a->flags = a->header[3];
    if (a->header[2] != 0xff) { /* a pose that is another record's first vectors */
        unsigned other = 8 + stride * a->header[2];
        if (other + stride > table->size) {
            free(a);
            return NULL;
        }
        a->initial = table->bytes + other;
    }
    if (a->data_at > table->size) {
        free(a);
        return NULL;
    }
    a->source = *table, a->record = index;
    read_holds(a);
    anim_reset(a);
    return a;
}

int pc_nm_anim_frames(const pc_nm_anim* a) { return a->frames; }

void pc_nm_anim_pose(pc_nm_anim* a, int time, pc_nm_pose* pose)
{
    if (time < a->time)
        anim_reset(a);
    /* the actor's frame counter is `time`: frames 1 .. time - 1 have been decoded */
    for (int frame = a->time > 1 ? a->time : 1; frame <= time - 1; frame++) {
        track_step(&a->root, frame, 0);
        for (int i = 0; i < a->bones; i++) {
            track_step(&a->bone[i], frame, 1);
            if (a->scale_live[i])
                track_step(&a->scale[i], frame, 0);
        }
    }
    if (time > a->time)
        a->time = time;
    pc_nm_pose_zero(a->m, pose);
    for (int k = 0; k < 3; k++)
        pose->offset[k] = (int16_t)(a->root.val[k] >> 1);
    for (int i = 0; i < a->bones && i < RENDER_MAX_BONES; i++) {
        for (int k = 0; k < 3; k++) {
            pose->rot[i][k] = (int16_t)(a->bone[i].val[k] >> 1);
            pose->scale[i][k] = a->scale_live[i] ? (uint16_t)a->scale[i].val[k] : a->scale_plain[i][k];
        }
        if (!(a->flags & 1))
            pose->scale[i][0] = pose->scale[i][1] = pose->scale[i][2] = 64;
    }
    /* the bones the events hold at this time: each where it stood when the event ran */
    if (a->no_locks || !a->holds)
        return;
    for (int b = 0; b < a->bones; b++) {
        int since = -1;
        for (int e = 0; e < a->holds; e++)
            if (a->hold[e].bone == b && a->hold[e].frame <= time)
                since = a->hold[e].hold ? a->hold[e].frame : -1;
        if (since < 0)
            continue;
        pc_nm_anim* before = pc_nm_anim_open(a->m, &a->source, a->record);
        if (!before)
            continue;
        before->no_locks = 1;
        pc_nm_pose p0;
        pc_nm_anim_pose(before, since > 1 ? since : 1, &p0); /* (the game, played live: the pose of the frame the event fires in fits best) */
        render_transform at[RENDER_MAX_BONES];
        if (pc_nm_pose_transforms(a->m, &p0, at) > b) {
            pose->locked[b] = 1;
            memcpy(pose->lock_rotation[b], at[b].rotation, sizeof pose->lock_rotation[b]);
            memcpy(pose->lock_place[b], at[b].translation, sizeof pose->lock_place[b]);
        }
        pc_nm_anim_close(before);
    }
}

void pc_nm_anim_close(pc_nm_anim* a) { free(a); }
void pc_nm_anim_holds(pc_nm_anim* a, int on) { a->no_locks = !on; }

int pc_nm_pose_rest(const pc_nm_model* m, pc_nm_pose* pose)
{
    pc_nm_pose_zero(m, pose);
    pc_nm_table table;
    int index;
    if (pc_nm_find_animation(m, NULL, NULL, 0, &table, &index))
        return -1;
    pc_nm_anim* a = pc_nm_anim_open(m, &table, index);
    if (!a)
        return -1;
    pc_nm_anim_pose(a, 1, pose);
    pc_nm_anim_close(a);
    return 0;
}
