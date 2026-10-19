// HUD elements (hud.h): the game's HUD elements as copies in a fixed pool,
// whether a claim on their kind is honoured (then the bridge turns the
// game's packets into no-ops), and rood.dev.show's moments: the game's own
// spawner in the field (the claim and the fallback both see a real
// element), held each vsync at one age until cleared; away from the field a
// synthetic element only claims draw. Nothing here allocates per frame.
#include "hud/hud_internal.h"

#include <cstdio>
#include <cstring>

extern "C" int pc_ui_menus __attribute__((weak));         // project_settings.cpp
extern "C" int pc_overlay_active(void) __attribute__((weak)); // dev_overlay.cpp: the presented frames' pass runs
extern "C" unsigned pc_vsync_count(void) __attribute__((weak));

extern "C" {
int pc_hud_classic_override;
}

namespace hud {
namespace {
Element elements[PC_HUD_POOL];
unsigned ticks; // pc_hud_tick calls (lua_probe has no vsync count)
unsigned next_id = 1;
KindStats kind_stats[PC_HUD_KINDS];

// rood.dev.show's hold, per kind (the game's element is found again each vsync: its
// record moves inside the game's pool).
struct Hold {
    bool active = false;
    bool synthetic = false;
    ShowArgs args;
    unsigned respawn_vsync = 0;
};
Hold holds[PC_HUD_KINDS];

#define SEEN_VSYNCS 4   // an element not drawn for longer is no longer shown (a field frame is 2-3 vsyncs)
#define KEEP_VSYNCS 120 // ... and its slot is reused after this (RPC hud {list} still shows it)
#define RESPAWN_VSYNCS 15

const char* const TIMINGS[] = { "too_fast", "fast", "success", "slow", "too_slow" };
// BATTLE.PRG func_800958A4's words and colours (PS1 BGR, 0x80 full).
const char* const TIMING_WORDS[] = { "TOO FAST!", "FAST!", "", "SLOW!", "TOO SLOW!" };
const char* const SUCCESS_WORDS[] = { "COOL!", "GOOD!", "EXCELLENT!", "RIGHT ON!", "PERFECT!", "WELL TIMED!", "NICE!",
    "GREAT!" };
const unsigned TIMING_COLOURS[] = { 0x808000, 0x608020, 0x408040, 0x208060, 0x8080 };

unsigned full(unsigned bgr)
{
    unsigned out = 0;
    for (int i = 0; i < 3; ++i) {
        unsigned v = (bgr >> (8 * i)) & 255u;
        v = v * 2 > 255 ? 255 : v * 2;
        out |= v << (8 * (2 - i));
    }
    return out;
}

bool single(int kind) { return kind == PC_HUD_STATUS; } // one at a time (the game removes the last)

Element* free_slot()
{
    Element* oldest = nullptr;
    for (Element& e : elements) {
        if (!e.used)
            return &e;
        if (!e.synthetic && !live(e) && (!oldest || e.last_seen < oldest->last_seen))
            oldest = &e;
    }
    return oldest;
}

void drop_synthetic(int kind)
{
    for (Element& e : elements)
        if (e.used && e.synthetic && (!kind || e.f.kind == kind))
            e = Element();
}

void make_synthetic(int kind, const ShowArgs& a)
{
    drop_synthetic(kind);
    Element* e = free_slot();
    if (!e)
        return;
    *e = Element();
    e->used = true;
    e->synthetic = true;
    e->id = next_id++;
    e->f.kind = kind;
    status_text(a.timing, a.variant, e->f.text, sizeof e->f.text);
    e->f.timing = a.timing;
    e->f.variant = a.variant;
    // where func_800958A4 centres it (44, 48), about its glyphs' box
    e->f.w = 8.0f * (float)std::strlen(e->f.text), e->f.h = 10;
    e->f.x = 44, e->f.y = 53;
    e->f.rgb = full(TIMING_COLOURS[a.timing]);
    e->f.age = a.age;
    e->f.life = 46;
    e->f.anchor = 1;
    e->held = a.hold;
    e->first_seen = e->last_seen = vsync();
}
} // namespace

Element* pool() { return elements; }
unsigned vsync() { return pc_vsync_count ? pc_vsync_count() : ticks; }
bool live(const Element& e) { return e.used && (e.synthetic || vsync() - e.last_seen <= SEEN_VSYNCS); }
KindStats& stats(int kind) { return kind_stats[kind > 0 && kind < PC_HUD_KINDS ? kind : 0]; }

const char* kind_name(int kind) { return kind == PC_HUD_STATUS ? "status" : ""; }
int kind_of(const char* name) { return name && !std::strcmp(name, "status") ? PC_HUD_STATUS : PC_HUD_NONE; }
int timing_of(const char* name)
{
    for (int i = 0; i < 5; ++i)
        if (!std::strcmp(name, TIMINGS[i]))
            return i;
    return -1;
}
const char* timing_name(int timing) { return timing >= 0 && timing < 5 ? TIMINGS[timing] : ""; }
void status_text(int timing, int variant, char* out, size_t size)
{
    const char* word = timing == 2 ? SUCCESS_WORDS[variant & 7] : timing >= 0 && timing < 5 ? TIMING_WORDS[timing] : "";
    std::snprintf(out, size, "%s", word);
}

bool show(int kind, const ShowArgs& args, std::string* error)
{
    if (kind != PC_HUD_STATUS) {
        if (error)
            *error = std::string("no show for HUD kind '") + kind_name(kind) + "'";
        return false;
    }
    ShowArgs a = args;
    a.timing = a.timing < 0 ? 0 : a.timing > 4 ? 4 : a.timing;
    a.variant &= 7;
    a.age = a.age < 4 ? 4 : a.age > 41 ? 41 : a.age; // past the slide-in, before the slide-out
    Hold& h = holds[kind];
    drop_synthetic(kind);
    if (pc_hud_bridge_field && pc_hud_bridge_field() && pc_hud_bridge_spawn && pc_hud_bridge_spawn(kind, a.timing, a.variant)) {
        h.active = a.hold;
        h.synthetic = false;
        h.args = a;
        h.respawn_vsync = vsync() + RESPAWN_VSYNCS;
        if (a.hold && pc_hud_bridge_hold)
            pc_hud_bridge_hold(kind, a.age);
        return true;
    }
    h.active = a.hold;
    h.synthetic = true;
    h.args = a;
    make_synthetic(kind, a);
    return true;
}

void clear(int kind)
{
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        if (kind && k != kind)
            continue;
        if (holds[k].active && !holds[k].synthetic && pc_hud_bridge_release)
            pc_hud_bridge_release(k);
        holds[k] = Hold();
        drop_synthetic(k);
    }
}

} // namespace hud

using namespace hud;

extern "C" int pc_hud_honoured(int kind)
{
    if (!claimed(kind))
        return 0;
    bool modern = &pc_ui_menus ? pc_ui_menus != 0 : true;
    if (!modern && !pc_hud_classic_override)
        return 0; // the classic look: the game's own drawing (typography only)
    return !pc_overlay_active || pc_overlay_active(); // nobody would draw it otherwise
}

extern "C" int pc_hud_record(const pc_hud_fields* f)
{
    if (!f || f->kind <= 0 || f->kind >= PC_HUD_KINDS)
        return 0;
    unsigned now = vsync();
    Element* e = nullptr;
    for (Element& x : elements)
        if (x.used && !x.synthetic && x.f.kind == f->kind && (x.f.key == f->key || single(f->kind))) {
            e = &x;
            break;
        }
    // a new appearance: none before, its age started over, or not drawn for a while
    if (!e || f->age < e->f.age || now - e->last_seen > SEEN_VSYNCS) {
        if (!e)
            e = free_slot();
        if (!e)
            return 0;
        *e = Element();
        e->used = true;
        e->id = next_id++;
        e->first_seen = now;
    }
    e->f = *f;
    e->f.text[PC_HUD_TEXT - 1] = 0;
    e->last_seen = now;
    e->held = holds[f->kind].active && !holds[f->kind].synthetic;
    e->suppressed = pc_hud_honoured(f->kind) != 0;
    KindStats& s = stats(f->kind);
    ++s.seen;
    ++(e->suppressed ? s.suppressed : s.fallback);
    return e->suppressed;
}

extern "C" void pc_hud_tick(void)
{
    ++ticks;
    unsigned now = vsync();
    bool field = pc_hud_bridge_field && pc_hud_bridge_field();
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        Hold& h = holds[k];
        if (!h.active || h.synthetic)
            continue;
        if (!field) { // left the field (a room load, the title): held on as a synthetic element
            h.synthetic = true;
            make_synthetic(k, h.args);
            continue;
        }
        if (pc_hud_bridge_hold && pc_hud_bridge_hold(k, h.args.age))
            continue;
        if ((int)(now - h.respawn_vsync) >= 0 && pc_hud_bridge_spawn) { // it went (replaced, a room change)
            pc_hud_bridge_spawn(k, h.args.timing, h.args.variant);
            h.respawn_vsync = now + RESPAWN_VSYNCS;
        }
    }
    for (Element& e : elements) {
        if (!e.used)
            continue;
        if (e.synthetic) {
            if (!e.held && ++e.f.age >= e.f.life)
                e = Element();
            else
                e.last_seen = now;
        } else if (now - e.last_seen > KEEP_VSYNCS) {
            e = Element();
        }
    }
}
