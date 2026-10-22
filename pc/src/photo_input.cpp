// rood.input's host side (photo.h; pc/docs/Extensions.md "Photo mode foundation"): while a plugin captures
// the input, boot_window.c gives it the keys (and the game nothing: no buttons, no stick), and each window
// tick the pad's, the keys' and the RPC's PS1 buttons become events; the safety: Escape (before the
// developer overlay) or Start gives the input back, and with nothing captured while the game is held gives
// every plugin's holds back (photo.cpp's pc_photo_release_all).
#include "photo.h"
#include "photo_internal.h"

#include <SDL3/SDL.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

extern "C" {
#include "boot_controller.h"
}

extern "C" {
int pc_photo_input_captured;
unsigned pc_photo_input_swallowed;
/* boot_window.c, dev_rpc.c */
void pc_window_release_keys(void) __attribute__((weak));
unsigned pc_rpc_buttons(void) __attribute__((weak));
}

namespace {
using photo::contains;
using photo::copy_out;
using photo::notify;
std::string input_owner;
std::deque<pc_photo_event> events;
unsigned key_bits, last_buttons, last_sources[3];
bool start_seen; /* a Start press while not captured: the safety release (on the edge) */
void push_event(int type, int down, const char* name, const char* device)
{
    if (events.size() >= 256)
        events.pop_front();
    pc_photo_event e;
    memset(&e, 0, sizeof e);
    e.type = type, e.down = down;
    snprintf(e.name, sizeof e.name, "%s", name);
    snprintf(e.device, sizeof e.device, "%s", device);
    events.push_back(e);
}
void input_release(const char* reason)
{
    if (input_owner.empty())
        return;
    push_event(PC_PHOTO_EVENT_RELEASED, 0, reason, "");
    notify(input_owner, "input", reason);
    input_owner.clear();
    pc_photo_input_captured = 0;
    key_bits = 0;
}
} // namespace

/* ------------------------------------------------------------ input */

extern "C" int pc_photo_input_capture(const char* owner, int on)
{
    if (!on) {
        if (input_owner == owner) {
            input_owner.clear();
            pc_photo_input_captured = 0;
            key_bits = 0;
        }
        return 0;
    }
    if (!input_owner.empty() && input_owner != owner)
        return -1;
    if (input_owner.empty()) {
        input_owner = owner;
        pc_photo_input_captured = 1;
        key_bits = 0;
        last_buttons = (pc_rpc_buttons ? pc_rpc_buttons() : 0) | (pc_controller_buttons() & ~pc_controller_stick_only());
        events.clear();
        if (pc_window_release_keys)
            pc_window_release_keys(); /* what the game held is let go */
    }
    return 0;
}
extern "C" int pc_photo_input_owner(char* out, int size)
{
    copy_out(out, size, input_owner);
    return !input_owner.empty();
}
extern "C" int pc_photo_input_events(pc_photo_event* out, int max)
{
    int n = 0;
    while (n < max && !events.empty()) {
        out[n++] = events.front();
        events.pop_front();
    }
    return n;
}
extern "C" void pc_photo_input_state_get(pc_photo_input_state* out)
{
    memset(out, 0, sizeof *out);
    out->buttons = last_buttons;
    pc_controller_left_stick(out->left);
    pc_controller_right_stick(out->right);
    out->l2 = pc_controller_trigger(0);
    out->r2 = pc_controller_trigger(1);
}
static const char* key_name(SDL_Keycode key, char* buffer, int size)
{
    const char* name = pc_key_name(key);
    if (name)
        return name;
    snprintf(buffer, (size_t)size, "%s", SDL_GetKeyName(key));
    for (char* p = buffer; *p; ++p)
        *p = *p == ' ' ? '_' : (char)tolower((unsigned char)*p);
    return buffer;
}
/* Escape, before the developer overlay (which opens on it) sees it: the safety. Captured: the input is
 * the game's again; nothing captured while a plugin holds the game: every plugin's holds given back. Its
 * release is taken too. */
static bool escape_down;
extern "C" int pc_photo_input_escape(const void* sdl_event)
{
    const SDL_Event* e = (const SDL_Event*)sdl_event;
    if ((e->type != SDL_EVENT_KEY_DOWN && e->type != SDL_EVENT_KEY_UP) || e->key.key != SDLK_ESCAPE)
        return 0;
    if (e->type == SDL_EVENT_KEY_UP) {
        bool taken = escape_down;
        escape_down = false;
        return taken;
    }
    if (e->key.repeat)
        return escape_down;
    if (!input_owner.empty()) {
        push_event(PC_PHOTO_EVENT_KEY, 1, "escape", "keyboard");
        input_release("escape");
        escape_down = true;
        return 1;
    }
    if (photo::paused()) {
        fprintf(stderr, "[photo] Escape: every plugin's photo holds released\n");
        pc_photo_release_all();
        escape_down = true;
        return 1;
    }
    return 0;
}
extern "C" int pc_photo_input_event(const void* sdl_event)
{
    const SDL_Event* e = (const SDL_Event*)sdl_event;
    if (e->type != SDL_EVENT_KEY_DOWN && e->type != SDL_EVENT_KEY_UP)
        return 0;
    bool down = e->type == SDL_EVENT_KEY_DOWN;
    if (input_owner.empty())
        return 0;
    if (e->key.repeat)
        return 1;
    char buffer[32];
    push_event(PC_PHOTO_EVENT_KEY, down, key_name(e->key.key, buffer, (int)sizeof buffer), "keyboard");
    unsigned bits = pc_keyboard_bits(e->key.key);
    key_bits = down ? key_bits | bits : key_bits & ~bits;
    return 1;
}
extern "C" void pc_photo_input_tick(void)
{
    /* (the left stick's D-pad is the camera's here, never the rows') */
    unsigned pad = pc_controller_buttons() & ~pc_controller_stick_only(), rpc = pc_rpc_buttons ? pc_rpc_buttons() : 0;
    unsigned held = key_bits | pad | rpc;
    const unsigned START = 1u << 11;
    pc_photo_input_swallowed &= held; /* (the safety's Start, until it is let go) */
    if (input_owner.empty()) {
        /* The safety: Start while a plugin holds the game, nothing captured (the edge). */
        bool start = (pad & START) != 0;
        if (start && !start_seen && photo::paused()) {
            fprintf(stderr, "[photo] Start: every plugin's photo holds released\n");
            pc_photo_release_all();
            pc_photo_input_swallowed |= START;
        }
        start_seen = start;
        last_buttons = held;
        return;
    }
    start_seen = (pad & START) != 0;
    unsigned changed = held ^ last_buttons;
    for (int b = 0; b < PC_PAD_BUTTONS && changed; b++) {
        unsigned bit = 1u << b;
        if (!(changed & bit))
            continue;
        const char* device = (rpc & bit) ? "rpc" : (pad & bit) ? "pad" : "keyboard";
        if (!(held & bit))
            device = (last_sources[2] & bit) ? "rpc" : (last_sources[1] & bit) ? "pad" : "keyboard";
        push_event(PC_PHOTO_EVENT_BUTTON, (held & bit) != 0, pc_pad_button_names[b], device);
    }
    last_buttons = held;
    last_sources[0] = key_bits, last_sources[1] = pad, last_sources[2] = rpc;
    if ((changed & START) && (held & START)) {
        input_release("start"); /* the safety: Start gives the input back (the game sees it from its next press) */
        pc_photo_input_swallowed |= START;
    }
}

/* ------------------------------------------------------------ the port's action buttons */

namespace {
struct Action {
    char name[24], device[12];
};
std::deque<Action> actions;
} // namespace
extern "C" void pc_photo_action(const char* name, const char* device)
{
    if (actions.size() >= 16)
        return;
    Action a;
    snprintf(a.name, sizeof a.name, "%s", name);
    snprintf(a.device, sizeof a.device, "%s", device);
    actions.push_back(a);
}
extern "C" int pc_photo_action_next(char* name, int name_size, char* device, int device_size)
{
    if (actions.empty())
        return 0;
    snprintf(name, (size_t)name_size, "%s", actions.front().name);
    snprintf(device, (size_t)device_size, "%s", actions.front().device);
    actions.pop_front();
    return 1;
}

namespace {
void release(const std::string& owner) { pc_photo_input_capture(owner.c_str(), 0); }
void owners(std::vector<std::string>& out)
{
    if (!input_owner.empty())
        out.push_back(input_owner);
}
void reset()
{
    input_owner.clear();
    events.clear();
    actions.clear();
    key_bits = 0;
    pc_photo_input_captured = 0; /* (these cross an engine reload: host-owned blocks) */
    pc_photo_input_swallowed = 0;
}
void json(std::string& out) { out += ",\"input\":" + photo::json_quote(input_owner); }
photo::Register part({ "input", release, owners, reset, json, nullptr });
} // namespace
