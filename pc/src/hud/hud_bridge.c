/* HUD elements, the game side (hud.h; battle builds, compiled with the game
 * headers): src's PC_HUD hook (hud_hook.h) reads the game's record into a
 * pc_hud_fields copy and, when hud_elements.cpp says a claim is honoured,
 * turns the element's sprites into no-ops (their packets' length 0: the
 * links stay, the GPU skips them; draw-mode packets kept, so nothing after
 * them changes). rood.dev.show's moments: the game's own spawner and the
 * hold (the record's age pinned each vsync).
 *
 * status: BATTLE.PRG's timing words (func_800958A4 from the label loop's
 * case 4: TOO FAST!, FAST!, SLOW!, TOO SLOW!, or COOL!, GOOD!, ... for a
 * well-timed chain; D_800F1BAC_t unkC the timing, displayTextLen the
 * success word, unkB the age; gone at 46). */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "hud.h"

extern short D_800F227E; /* the labels in D_800F1BAC */
int vs_battle_renderBattleAbilityTimingResult(int arg0);

void pc_hud_element(int kind, void* record, void* first, void* end);

#define STATUS_LIFE 46

static int status_field(void)
{
    return vs_battle_roomData.unk0 == 1 && vs_battle_characterState != NULL && D_800F1BAC != NULL
        && D_800F227E >= 0 && D_800F227E <= 32;
}

/* The game's status record (one at a time: its spawner removes the last). */
static D_800F1BAC_t* status_record(void)
{
    D_800F1BAC_t* p;
    int i;
    if (!status_field())
        return NULL;
    p = *D_800F1BAC;
    for (i = 0; i < D_800F227E; ++i, ++p)
        if (p->unk8.u16 == 4)
            return p;
    return NULL;
}

/* A PS1 colour word's BGR at full brightness (0x80 -> 0xFF) as 0xRRGGBB. */
static unsigned full_rgb(unsigned bgr)
{
    unsigned out = 0, v;
    int i;
    for (i = 0; i < 3; ++i) {
        v = (bgr >> (8 * i)) & 255u;
        v = v * 2 > 255 ? 255 : v * 2;
        out |= v << (8 * (2 - i));
    }
    return out;
}

/* The sprites (GP0 0x64-0x67) among the packets [first, end): their box and colour; with
 * suppress, each one's length set to 0. */
static int sprites(void* first, void* end, pc_hud_fields* f, int suppress)
{
    u_int* w = first;
    u_int* stop = end;
    int count = 0, x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    if (!w || !stop || stop <= w || stop - w > 8 * 64)
        return 0;
    while (w + 1 < stop) {
        u_int len = w[0] >> 24, code = w[1] >> 24;
        if (len == 4 && (code & 0xfc) == 0x64 && w + 5 <= stop) {
            int x = (short)(w[2] & 0xffff), y = (short)(w[2] >> 16);
            int sw = (int)(w[4] & 0xffff), sh = (int)(w[4] >> 16);
            if (!count)
                f->rgb = full_rgb(w[1] & 0xffffff);
            x0 = x < x0 ? x : x0, y0 = y < y0 ? y : y0;
            x1 = x + sw > x1 ? x + sw : x1, y1 = y + sh > y1 ? y + sh : y1;
            if (suppress)
                w[0] &= 0x00ffffff;
            ++count;
        }
        w += 1 + len;
    }
    if (count && !suppress) {
        f->x = (float)(x0 + x1) * 0.5f, f->y = (float)(y0 + y1) * 0.5f;
        f->w = (float)(x1 - x0), f->h = (float)(y1 - y0);
    }
    return count;
}

/* Each word as func_800958A4 prints it (its strings' $ and wide spaces left out). */
static const char* const timing_words[] = { "TOO FAST!", "FAST!", "", "SLOW!", "TOO SLOW!" };
static const char* const success_words[] = { "COOL!", "GOOD!", "EXCELLENT!", "RIGHT ON!", "PERFECT!",
    "WELL TIMED!", "NICE!", "GREAT!" };

void pc_hud_element(int kind, void* record, void* first, void* end)
{
    pc_hud_fields f = { 0 };
    D_800F1BAC_t* p = record;
    const char* word;
    int i;
    if (kind != PC_HUD_STATUS || !p)
        return;
    f.kind = kind;
    f.key = p;
    f.timing = p->unkC < 5 ? p->unkC : 0;
    f.variant = p->displayTextLen & 7;
    word = f.timing == 2 ? success_words[f.variant] : timing_words[f.timing];
    for (i = 0; word[i] && i < PC_HUD_TEXT - 1; ++i)
        f.text[i] = word[i];
    f.age = p->unkB;
    f.life = STATUS_LIFE;
    f.anchor = 1; /* PC_ANCHOR_LEFT_EDGE */
    if (!sprites(first, end, &f, 0))
        return;
    if (pc_hud_record(&f))
        sprites(first, end, &f, 1);
}

int pc_hud_bridge_field(void) { return status_field(); }

int pc_hud_bridge_spawn(int kind, int timing, int variant)
{
    D_800F1BAC_t* p;
    if (kind != PC_HUD_STATUS || !status_field())
        return 0;
    vs_battle_renderBattleAbilityTimingResult(timing);
    p = status_record();
    if (!p)
        return 0;
    p->displayTextLen = (char)variant; /* (the spawner picks one at random) */
    return 1;
}

int pc_hud_bridge_hold(int kind, int age)
{
    D_800F1BAC_t* p = kind == PC_HUD_STATUS ? status_record() : NULL;
    if (!p)
        return 0;
    p->unkB = (char)age;
    return 1;
}

void pc_hud_bridge_release(int kind)
{
    D_800F1BAC_t* p = kind == PC_HUD_STATUS ? status_record() : NULL;
    if (p && p->unkB < 42)
        p->unkB = 42; /* its slide out */
}
