/* Anti-aliasing settings (antialias.h) and SMAA's lookup textures. */
#include "antialias.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* SMAA's precomputed textures (MIT; pc/third_party/smaa/UPSTREAM.txt). */
#include "../third_party/smaa/Textures/AreaTex.h"
#include "../third_party/smaa/Textures/SearchTex.h"

int pc_gl_aa = PC_AA_FXAA;
int pc_aa_msaa;
float pc_aa_ssaa;
int pc_aa_downsample = PC_AA_DOWNSAMPLE_AREA;
int pc_aa_geometry_edges = 1;
int pc_aa_autodetect_pending;
const char* const pc_aa_downsample_names[3] = { "area", "lanczos", "bilinear" };

int pc_aa_parse(const char* text)
{
    int post = PC_AA_OFF, msaa = 0;
    float ssaa = 0;
    char word[32];
    const char* p = text ? text : "";
    while (*p == ' ' || *p == '\t')
        ++p;
    if (!*p)
        return 0;
    while (*p && *p != '\n' && *p != '\r') {
        size_t n = strcspn(p, "+ \t\r\n");
        if (n == 0 || n >= sizeof word)
            return 0;
        memcpy(word, p, n);
        word[n] = 0;
        p += n;
        while (*p == '+' || *p == ' ' || *p == '\t')
            ++p;
        if (!strcmp(word, "off") || !strcmp(word, "none") || !strcmp(word, "0"))
            continue;
        if (!strcmp(word, "fxaa") || !strcmp(word, "1"))
            post = PC_AA_FXAA;
        else if (!strcmp(word, "smaa"))
            post = PC_AA_SMAA;
        else if (!strncmp(word, "msaa", 4)) {
            int samples = word[4] ? atoi(word + 4) : 4;
            if (samples != 1 && samples != 2 && samples != 4 && samples != 8)
                return 0;
            msaa = samples > 1 ? samples : 0;
        } else if (!strncmp(word, "ssaa", 4)) {
            float factor = word[4] ? (float)atof(word + 4) : 2.0f;
            if (!(factor >= 1.0f && factor <= 4.0f))
                return 0;
            ssaa = factor > 1.0f ? factor : 0;
        } else
            return 0;
    }
    pc_gl_aa = post;
    pc_aa_msaa = msaa;
    pc_aa_ssaa = ssaa;
    return 1;
}

const char* pc_aa_string(void)
{
    static char text[64];
    static const char* const posts[3] = { "", "fxaa", "smaa" };
    text[0] = 0;
    if (pc_aa_msaa > 1)
        snprintf(text + strlen(text), sizeof text - strlen(text), "msaa%d", pc_aa_msaa);
    if (pc_aa_ssaa > 1.0f)
        snprintf(text + strlen(text), sizeof text - strlen(text), "%sssaa%g", text[0] ? "+" : "", pc_aa_ssaa);
    if (pc_gl_aa > 0 && pc_gl_aa < 3)
        snprintf(text + strlen(text), sizeof text - strlen(text), "%s%s", text[0] ? "+" : "", posts[pc_gl_aa]);
    if (!text[0])
        snprintf(text, sizeof text, "off");
    return text;
}

const char* pc_aa_autodetect(int integrated) { return integrated ? "smaa" : "msaa4+smaa"; }

int pc_aa_ssaa_scale(int view_h, int display_h)
{
    if (!(pc_aa_ssaa > 1.0f) || view_h <= 0 || display_h <= 0)
        return 0;
    int scale = (int)ceilf(pc_aa_ssaa * (float)view_h / (float)display_h - 0.01f);
    return scale < 1 ? 1 : scale > 8 ? 8 : scale;
}

const unsigned char* pc_smaa_area_texture(int* width, int* height)
{
    *width = AREATEX_WIDTH, *height = AREATEX_HEIGHT;
    return areaTexBytes;
}
const unsigned char* pc_smaa_search_texture(int* width, int* height)
{
    *width = SEARCHTEX_WIDTH, *height = SEARCHTEX_HEIGHT;
    return searchTexBytes;
}
