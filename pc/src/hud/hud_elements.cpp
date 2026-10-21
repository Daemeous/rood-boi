// HUD elements (hud.h): the game's HUD elements as copies in a fixed pool,
// whether a claim on their kind is honoured (then the bridge turns the
// game's packets into no-ops), and rood.dev.show's moments: the game's own
// spawner in the field (the claim and the fallback both see a real
// element), held each vsync at one age until cleared; away from the field a
// synthetic element only claims draw. Nothing here allocates per frame.
#include "hud/hud_internal.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

extern "C" int pc_ui_menus __attribute__((weak));         // project_settings.cpp
extern "C" int pc_overlay_active(void) __attribute__((weak)); // dev_overlay.cpp: the presented frames' pass runs
extern "C" unsigned pc_vsync_count(void) __attribute__((weak));
// fast_travel_prompt.cpp: the Travel prompt's place and size from the settings (a synthetic element's)
extern "C" void pc_fast_travel_prompt_fields(pc_hud_fields* f) __attribute__((weak));
// savestate_notice.cpp: a notice's place and size (a synthetic element's)
extern "C" void pc_savestate_notice_fields(pc_hud_fields* f) __attribute__((weak));

// classic_parity.c: the parity mode (the game draws, the Lua draw is compared); absent in lua_probe
extern "C" int pc_classic_parity_on(void) __attribute__((weak));

extern "C" {
int pc_hud_classic_override;
// Per kind (hud.h): on for the kinds whose classic Lua draw passes the parity gate
// (pc/tests/classic_parity.py): the timing word, the chain count, the gauges.
int pc_hud_classic_status = 1, pc_hud_classic_chain = 1, pc_hud_classic_buff = 0, pc_hud_classic_damage = 0,
    pc_hud_classic_gauges = 1;
int pc_hud_classic_default(int kind)
{
    switch (kind) {
    case PC_HUD_STATUS: return pc_hud_classic_status != 0;
    case PC_HUD_CHAIN: return pc_hud_classic_chain != 0;
    case PC_HUD_BUFF: return pc_hud_classic_buff != 0;
    case PC_HUD_DAMAGE: return pc_hud_classic_damage != 0;
    case PC_HUD_GAUGES: return pc_hud_classic_gauges != 0;
    }
    return 0;
}
}

namespace hud {
namespace {
Element elements[PC_HUD_POOL];
Element* recorded; // the last pc_hud_record's
unsigned ticks; // pc_hud_tick calls (lua_probe has no vsync count)
unsigned next_id = 1;
KindStats kind_stats[PC_HUD_KINDS];

// rood.dev.show's hold, per kind (the game's element is found again each vsync: its
// record moves inside the game's pool).
struct Hold {
    bool active = false;
    bool synthetic = false;
    ShowArgs args;
    pc_hud_spawn spawn = {}; // args for the bridge
    unsigned respawn_vsync = 0;
    unsigned close_vsync = 0; // name_window shown without hold: closed then
};
Hold holds[PC_HUD_KINDS];

#define SEEN_VSYNCS 4   // an element not drawn for longer is no longer shown (a field frame is 2-3 vsyncs)
#define KEEP_VSYNCS 120 // ... and its slot is reused after this (RPC hud {list} still shows it)
#define RESPAWN_VSYNCS 15

const char* const KINDS[PC_HUD_KINDS] = { "", "status", "chain", "buff", "damage", "travel", "notice",
    "name_window", "gauges" };
const char* const TIMINGS[] = { "too_fast", "fast", "success", "slow", "too_slow" };
// BATTLE.PRG func_800958A4's words and colours (PS1 BGR, 0x80 full).
const char* const TIMING_WORDS[] = { "TOO FAST!", "FAST!", "", "SLOW!", "TOO SLOW!" };
const char* const SUCCESS_WORDS[] = { "COOL!", "GOOD!", "EXCELLENT!", "RIGHT ON!", "PERFECT!", "WELL TIMED!", "NICE!",
    "GREAT!" };
const unsigned TIMING_COLOURS[] = { 0x808000, 0x608020, 0x408040, 0x208060, 0x8080 };
// D_80069144's and D_8006917C's words (hud_bridge.c), the damage pools
const char* const SLOTS[] = { "WEAPON", "SHIELD", "R.ARM", "L.ARM", "HEAD", "BODY", "LEGS" };
const char* const STATS[] = { "HUMAN", "BEAST", "UNDEAD", "PHANTOM", "DRAGON", "EVIL", "PHYSICS", "AIR", "FIRE",
    "EARTH", "WATER", "LIGHT", "DARKNESS" };
const char* const POOLS[] = { "hp", "mp", "stat" };
// The ages a held element is kept at: past its slide-in, before its slide-out or end.
const int AGE_MIN[PC_HUD_KINDS] = { 0, 4, 4, 0, 0, 0, 0, 0, 0 };
const int AGE_MAX[PC_HUD_KINDS] = { 0, 41, 86, 60, 30, 0, 0, 0, 0 };
const int LIFE[PC_HUD_KINDS] = { 0, 46, 91, 61, 31, 0, 150, 120, 0 }; // (name_window: a show without hold, vsyncs)

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

// one at a time (the game removes the last; one prompt)
bool single(int kind)
{
    return kind == PC_HUD_STATUS || kind == PC_HUD_CHAIN || kind == PC_HUD_TRAVEL || kind == PC_HUD_NOTICE
        || kind == PC_HUD_NAME_WINDOW || kind == PC_HUD_GAUGES;
}

Appearance appeared[64];
unsigned appeared_head, appeared_count;
void appear(const Element& e)
{
    appeared[(appeared_head + appeared_count) % 64] = { (int)(&e - elements), e.id };
    if (appeared_count < 64)
        ++appeared_count;
    else
        appeared_head = (appeared_head + 1) % 64;
}

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

void add_part(pc_hud_fields& f, float x, float y, float w, float h, const char* text)
{
    if (f.part_count >= PC_HUD_PARTS)
        return;
    pc_hud_part& p = f.parts[f.part_count++];
    p.x = x, p.y = y, p.w = w, p.h = h;
    std::snprintf(p.text, sizeof p.text, "%s", text);
}

// About where and how big the game draws it (its own draw functions' places).
void synthetic_fields(int kind, const ShowArgs& a, pc_hud_fields& f)
{
    f.kind = kind;
    f.age = a.age;
    f.life = LIFE[kind];
    f.anchor = PC_HUD_ANCHOR_LEFT;
    char digits[8];
    std::snprintf(digits, sizeof digits, "%d", a.value);
    switch (kind) {
    case PC_HUD_STATUS: // func_800958A4 at (44, 48), centred about its glyphs' box
        status_text(a.timing, a.variant, f.text, sizeof f.text);
        f.timing = a.timing;
        f.variant = a.variant;
        f.w = 8.0f * (float)std::strlen(f.text), f.h = 10;
        f.x = 44, f.y = 53;
        f.rgb = full(TIMING_COLOURS[a.timing]);
        break;
    case PC_HUD_CHAIN: // func_80095A4C at (44, 64)
        f.value = a.value;
        std::snprintf(f.text, sizeof f.text, a.value == 1 ? "%d CHAIN" : "%d CHAINS", a.value);
        f.w = 8.0f * (float)std::strlen(f.text), f.h = 10;
        f.x = 44, f.y = 69;
        f.rgb = full(0x1040A0);
        break;
    case PC_HUD_BUFF: { // func_8009695C at (8, 172): two words, a sign, the digits
        f.slot = a.slot, f.stat = a.stat, f.sign = a.sign, f.value = a.value;
        std::snprintf(f.text, sizeof f.text, "%s %s %c%s", SLOTS[a.slot], STATS[a.stat], a.sign < 0 ? '-' : '+',
            digits);
        float x = 8, w1 = 5.0f * (float)std::strlen(SLOTS[a.slot]), w2 = 5.0f * (float)std::strlen(STATS[a.stat]);
        add_part(f, x, 172, w1, 7, SLOTS[a.slot]);
        add_part(f, x += w1 + 2, 172, w2, 7, STATS[a.stat]);
        add_part(f, x += w2 + 2, 171, 8, 8, a.sign < 0 ? "-" : "+");
        add_part(f, x += 10, 171, 5.0f * (float)std::strlen(digits) + 1, 10, digits);
        f.x = (8 + x + f.parts[3].w) * 0.5f, f.y = 176;
        f.w = x + f.parts[3].w - 8, f.h = 10;
        f.rgb = a.sign < 0 ? 0xFF8000 : 0x0080FF;
        break;
    }
    case PC_HUD_DAMAGE: { // func_80094B0C over an actor: the picture's middle here
        f.value = a.value, f.sign = a.sign, f.pool = a.pool, f.stat = a.stat;
        std::snprintf(f.text, sizeof f.text, "%s", digits);
        float half = 3.0f * (float)std::strlen(digits);
        f.x = 160, f.y = 120;
        f.w = half * 2 + 2, f.h = 12;
        add_part(f, f.x - half, f.y - 6, f.w, 12, digits);
        f.anchor = PC_HUD_ANCHOR_CENTRE;
        static const unsigned colours[4] = { 0xFFFFFF, 0x00FF00, 0xFF00FF, 0x00FFFF }; // its CVECTORs
        f.rgb = colours[(a.pool == 1 ? 2 : 0) + (a.sign > 0)];
        break;
    }
    case PC_HUD_NAME_WINDOW: // text box 7: a band across the bottom (func_800CD158), 43 rows for one line
        std::snprintf(f.text, sizeof f.text, "%s", a.text[0] ? a.text : "NAME WINDOW");
        f.variant = a.variant, f.value = a.value;
        f.w = 320, f.h = 43;
        f.x = 160, f.y = 240 - 21.5f;
        f.rgb = 0xFFFFFF;
        f.anchor = PC_HUD_ANCHOR_CENTRE;
        break;
    case PC_HUD_TRAVEL:
        std::snprintf(f.text, sizeof f.text, "TRAVEL");
        f.anchor = PC_HUD_ANCHOR_PICTURE;
        f.x = 304, f.y = 16, f.variant = 2, f.unit = 1.0f / 3.0f;
        f.glyph = 16; // PC_PROMPT_TRAVEL (button_prompts.h)
        f.rgb = 0xFFFFFF;
        if (pc_fast_travel_prompt_fields)
            pc_fast_travel_prompt_fields(&f);
        break;
    case PC_HUD_NOTICE: // the save state slots' line, at the top of the picture, centred
        std::snprintf(f.text, sizeof f.text, "%s", a.text[0] ? a.text : "Saved to slot 1");
        f.value = a.value;
        f.anchor = PC_HUD_ANCHOR_PICTURE;
        f.x = 160, f.y = 30, f.variant = 1, f.unit = 1.0f / 3.0f;
        f.rgb = 0xFFFFFF;
        if (pc_savestate_notice_fields)
            pc_savestate_notice_fields(&f);
        break;
    }
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
    synthetic_fields(kind, a, e->f);
    e->held = a.hold;
    e->first_seen = e->last_seen = vsync();
    appear(*e);
}

pc_hud_spawn spawn_of(const ShowArgs& a)
{
    pc_hud_spawn s = {};
    s.timing = a.timing, s.variant = a.variant, s.age = a.age, s.value = a.value, s.sign = a.sign;
    s.pool = a.pool, s.stat = a.stat, s.slot = a.slot;
    return s;
}
} // namespace

Element* pool() { return elements; }
int take_appearances(Appearance* out, int max)
{
    int n = 0;
    while (appeared_count && n < max) {
        out[n++] = appeared[appeared_head];
        appeared_head = (appeared_head + 1) % 64;
        --appeared_count;
    }
    return n;
}
Element* last_recorded() { return recorded; }
bool classic_look() { return &pc_ui_menus && pc_ui_menus == 0; }
unsigned vsync() { return pc_vsync_count ? pc_vsync_count() : ticks; }
bool live(const Element& e) { return e.used && (e.synthetic || vsync() - e.last_seen <= SEEN_VSYNCS); }
KindStats& stats(int kind) { return kind_stats[kind > 0 && kind < PC_HUD_KINDS ? kind : 0]; }

const char* kind_name(int kind) { return kind > 0 && kind < PC_HUD_KINDS ? KINDS[kind] : ""; }
int kind_of(const char* name)
{
    for (int k = 1; name && k < PC_HUD_KINDS; ++k)
        if (!std::strcmp(name, KINDS[k]))
            return k;
    return PC_HUD_NONE;
}
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
const char* slot_word(int slot) { return slot >= 0 && slot < 7 ? SLOTS[slot] : ""; }
const char* stat_word(int stat) { return stat >= 0 && stat < 13 ? STATS[stat] : ""; }
const char* pool_name(int pool) { return pool >= 0 && pool < 3 ? POOLS[pool] : ""; }
int word_index(const char* name, const char* (*word)(int), int count)
{
    for (int i = 0; name && i < count; ++i)
        if (!strcasecmp(name, word(i)))
            return i;
    return -1;
}

bool show(int kind, const ShowArgs& args, std::string* error)
{
    if (kind <= 0 || kind >= PC_HUD_KINDS || kind == PC_HUD_GAUGES) {
        if (error)
            *error = kind == PC_HUD_GAUGES ? "the gauges have no show: they are up whenever the field is"
                                           : "no show for that HUD kind (" HUD_KIND_LIST ")";
        return false;
    }
    ShowArgs a = args;
    a.timing = a.timing < 0 ? 0 : a.timing > 4 ? 4 : a.timing;
    a.variant = kind == PC_HUD_NAME_WINDOW ? a.variant : a.variant & 7;
    a.age = a.age < AGE_MIN[kind] ? AGE_MIN[kind] : a.age > AGE_MAX[kind] ? AGE_MAX[kind] : a.age;
    if (kind == PC_HUD_NAME_WINDOW) { // an action's name (a technique, as a battle shows it), its message
        a.value = a.value < 0 ? 184 : a.value > 255 ? 255 : a.value;
        a.variant = args.variant ? args.variant : 11;
    } else {
        if (a.value < 0)
            a.value = kind == PC_HUD_CHAIN ? 3 : kind == PC_HUD_DAMAGE ? 123 : 1;
        a.value = a.value > 999 ? 999 : a.value < 1 ? 1 : a.value;
    }
    if (!a.sign)
        a.sign = kind == PC_HUD_DAMAGE ? -1 : 1;
    a.sign = a.sign < 0 ? -1 : 1;
    a.pool = a.pool < 0 ? 0 : a.pool > 2 ? 2 : a.pool;
    int stats = kind == PC_HUD_BUFF ? 12 : 5; // (damage: func_80094E18's icons, unkE 3..8)
    a.stat = a.stat < 0 ? 0 : a.stat > stats ? stats : a.stat;
    a.slot = a.slot < 0 ? 0 : a.slot > 6 ? 6 : a.slot;
    if (kind == PC_HUD_TRAVEL)
        a.hold = true; // (the forced prompt has no life of its own: shown until clear)
    if (kind == PC_HUD_NOTICE) // (the port's own: never the game's spawner, always a synthetic element)
        a.value = args.value < 0 ? 1 : args.value;
    Hold& h = holds[kind];
    if (h.active && !h.synthetic && pc_hud_bridge_release) // (the last one goes)
        pc_hud_bridge_release(kind, &h.spawn);
    drop_synthetic(kind);
    pc_hud_spawn sp = spawn_of(a);
    if (pc_hud_bridge_field && pc_hud_bridge_field() && pc_hud_bridge_spawn && pc_hud_bridge_spawn(kind, &sp)) {
        h.active = a.hold;
        h.synthetic = false;
        h.args = a;
        h.spawn = sp;
        h.respawn_vsync = vsync() + RESPAWN_VSYNCS;
        if (kind == PC_HUD_NAME_WINDOW) { // (nothing in the field closes it: a show without hold is closed after its life)
            h.active = true;
            h.close_vsync = a.hold ? 0 : vsync() + (unsigned)LIFE[kind];
        }
        if (a.hold && pc_hud_bridge_hold)
            pc_hud_bridge_hold(kind, &sp);
        return true;
    }
    h.active = a.hold;
    h.synthetic = true;
    h.args = a;
    h.spawn = sp;
    make_synthetic(kind, a);
    return true;
}

int held_moments(HeldMoment* out, int max)
{
    int n = 0;
    for (int k = 1; k < PC_HUD_KINDS && n < max; ++k)
        if (holds[k].active && !holds[k].close_vsync)
            out[n++] = { k, holds[k].args };
    return n;
}

void adopt(int kind, const ShowArgs& args)
{
    if (kind <= 0 || kind >= PC_HUD_KINDS)
        return;
    Hold& h = holds[kind];
    h = Hold();
    h.active = true;
    h.args = args;
    h.args.hold = true;
    h.spawn = spawn_of(h.args);
    if (pc_hud_bridge_field && pc_hud_bridge_field()) {
        if (kind == PC_HUD_TRAVEL && pc_hud_bridge_spawn) // (the forced prompt is the host's: not in the state)
            pc_hud_bridge_spawn(kind, &h.spawn);
        h.respawn_vsync = vsync() + RESPAWN_VSYNCS;
        return;
    }
    h.synthetic = true;
    make_synthetic(kind, h.args);
}

void clear(int kind)
{
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        if (kind && k != kind)
            continue;
        if (holds[k].active && !holds[k].synthetic && pc_hud_bridge_release)
            pc_hud_bridge_release(k, &holds[k].spawn);
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
    if (hud::classic_look()) { // the claim draws at the game's hook, in the classic flavour, when
        if (pc_classic_parity_on && pc_classic_parity_on())
            return 0; // (the parity mode: the game draws, the Lua draw only compared)
        if (pc_hud_classic_override)
            return 1; // every kind (the opt-in for draws not yet through the parity gate)
        return claimed_classic(kind) && pc_hud_classic_default(kind); // a classic claim on a kind that passed
    }
    if (kind == PC_HUD_GAUGES)
        return 0; // (the classic look's only: the modern look hides the status display)
    return !pc_overlay_active || pc_overlay_active(); // nobody would draw it otherwise
}

extern "C" int pc_hud_record(const pc_hud_fields* f)
{
    if (!f || f->kind <= 0 || f->kind >= PC_HUD_KINDS)
        return 0;
    unsigned now = vsync();
    Element* e = nullptr;
    bool fresh = false;
    for (Element& x : elements)
        if (x.used && !x.synthetic && x.f.kind == f->kind && (x.f.key == f->key || single(f->kind))) {
            e = &x;
            break;
        }
    if (!e) // a record the game moved (its pool closes up when one goes): the same number, a little older
        for (Element& x : elements)
            if (x.used && !x.synthetic && x.f.kind == f->kind && x.last_seen != now && f->age >= x.f.age
                && f->age - x.f.age <= 4 && x.f.value == f->value && x.f.sign == f->sign && x.f.pool == f->pool
                && x.f.slot == f->slot && x.f.stat == f->stat) {
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
        fresh = true;
    }
    e->f = *f;
    e->f.text[PC_HUD_TEXT - 1] = 0;
    e->last_seen = now;
    const Hold& h = holds[f->kind];
    e->held = h.active && !h.synthetic && !h.close_vsync
        && (f->kind == PC_HUD_NAME_WINDOW ? f->value == h.spawn.value
                                          : single(f->kind) || (f->value == h.spawn.value && f->sign == h.spawn.sign));
    e->suppressed = pc_hud_honoured(f->kind) != 0;
    recorded = e;
    if (fresh)
        appear(*e);
    KindStats& s = stats(f->kind);
    ++s.seen;
    ++(e->suppressed ? s.suppressed : s.fallback);
    // the classic flavour: drawn now, at the game's hook (the Travel prompt and notices have none: never classic)
    return e->suppressed ? (classic_look() && f->kind != PC_HUD_TRAVEL && f->kind != PC_HUD_NOTICE ? 2 : 1) : 0;
}

extern "C" void pc_hud_tick(void)
{
    ++ticks;
    unsigned now = vsync();
    bool field = pc_hud_bridge_field && pc_hud_bridge_field();
    pc_hud_fields name;
    if (field && pc_hud_bridge_name_window && pc_hud_bridge_name_window(&name))
        pc_hud_record(&name); // (an event: never claimed, the game draws it)
    for (int k = 1; k < PC_HUD_KINDS; ++k) {
        Hold& h = holds[k];
        if (!h.active || h.synthetic)
            continue;
        if (h.close_vsync) { // a name window shown without hold: its life
            if ((int)(now - h.close_vsync) >= 0) {
                if (pc_hud_bridge_release)
                    pc_hud_bridge_release(k, &h.spawn);
                h = Hold();
            }
            continue;
        }
        if (!field) { // left the field (a room load, the title): held on as a synthetic element
            h.synthetic = true;
            make_synthetic(k, h.args);
            continue;
        }
        if (pc_hud_bridge_hold && pc_hud_bridge_hold(k, &h.spawn))
            continue;
        if ((int)(now - h.respawn_vsync) >= 0 && pc_hud_bridge_spawn) { // it went (replaced, a room change)
            pc_hud_bridge_spawn(k, &h.spawn);
            h.respawn_vsync = now + RESPAWN_VSYNCS;
        }
    }
    for (Element& e : elements) {
        if (!e.used)
            continue;
        if (e.synthetic) {
            if (!e.held && ++e.f.age >= e.f.life && e.f.life > 0)
                e = Element();
            else
                e.last_seen = now;
        } else if (now - e.last_seen > KEEP_VSYNCS) {
            e = Element();
        }
    }
}
