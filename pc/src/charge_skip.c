/* Battle charging skip: the host side (settings qol_skip_charging and
 * qol_skip_charging_who, System > Gameplay; off by default).
 *
 * An action with an effect (a spell, an item, a Break Art) winds up before
 * it takes effect: BATTLE's action state (D_800F196C 2) runs _cameraMode 4
 * (the camera zooms in), 5 (the casting: its name window, the cast
 * animation and sound, the effect's file loading) and 6 (the cast flash
 * fades), then 7 releases it. The action is committed before all of that
 * (146C.c func_80070F28: costs, item used, hits, damage and statuses rolled),
 * so the wind-up decides nothing. Skip takes out its hold (charge_skip.inc,
 * the game side): the camera's move to the caster and the name window are
 * the game's own, then the cast sound is stopped, the cast turn applied at
 * once, the magic circle ends at once, the effect's files load back to back,
 * and the release starts as soon as they are in. The few vsyncs of the hold
 * that still run (about 8, 4 game frames) are unpaced and unpresented: the
 * window holds the last picture until the release's first frame. The name
 * window stays up past the release until it has been shown for
 * qol_skip_charging_name seconds. Nothing is sped up: every vsync that runs
 * is an ordinary game vsync, there are just fewer. Some of the hold may be
 * shown first, paced (qol_skip_charging_hold_mine / _hold_enemy, seconds
 * from the camera's arrival; charge_skip.inc): a beat to see an enemy's
 * action coming before it is released.
 *
 * Here: which vsyncs are hidden (from the game side's answer, fresh every
 * vsync), the counters for RPC skip_charging, and the restart of the
 * temporal filters after a hidden stretch (gpu_hw.c pc_hw_history_cut). */
#include <stdio.h>
#include <time.h>

/* project_settings.cpp: 0 off, 1 skip; 0 my actions (Ashley's), 1 every
 * actor's (enemies' spells and arts too). */
extern int pc_qol_skip_charging, pc_qol_skip_charging_who;
/* project_settings.cpp: how long a skipped wind-up's name window stays up
 * at least, seconds (0..2; 0 closes it at the release). */
extern float pc_qol_skip_charging_name;
/* project_settings.cpp: how much of a skipped wind-up's hold is shown first,
 * seconds (0..1.5), for Ashley's actions and for the others'. */
extern float pc_qol_skip_charging_hold_mine, pc_qol_skip_charging_hold_enemy;
/* Which actions Skip applies to (RPC skip_charging kinds, a developer mask):
 * 1 spells (actions below 0x8D), 2 enemies' arts (0x8D..0xB7), 4 Break Arts
 * (0xB8..0xDF), 8 items (0xE0 on). */
int pc_charge_skip_kinds = 15;
/* charge_skip.inc (battle builds): whether this vsync is in an action's
 * wind-up (*phase: its _cameraMode, 4..6), and whether Skip applies to it. */
extern int pc_charge_skip_state(int* phase, int* skipping, int* actor, int* action) __attribute__((weak));
/* Test aids (charge_skip.inc): the view and the caster's facing; BATTLE's
 * state; the random numbers seeded. */
extern void pc_charge_skip_view(int out[9], int actor) __attribute__((weak));
extern int pc_charge_skip_battle_state(int* camera, int* actor, int* action) __attribute__((weak));
extern void pc_charge_skip_reseed(unsigned seed) __attribute__((weak));
extern int pc_charge_skip_window(void) __attribute__((weak));
extern int pc_charge_skip_ability(int out[4]) __attribute__((weak));
extern unsigned pc_vsync_count(void); /* boot_runtime.c */
extern int pc_charge_skip_trace(char* out, int size) __attribute__((weak));
extern int pc_charge_skip_pump __attribute__((weak)), pc_charge_skip_pumped __attribute__((weak));
/* charge_skip.inc: the name window up (its text), and whether it is held. */
extern int pc_charge_skip_name_window(unsigned char* out, int size, int* held) __attribute__((weak));
/* The last wind-up's trace (RPC skip_charging trace: one entry a vsync,
 * charge_skip.inc pc_charge_skip_trace). */
static char trace[4096];
static int trace_len;
extern unsigned pc_hw_history_cut __attribute__((weak)); /* gpu_hw.c */

static int hidden;   /* this vsync is not presented */
static int skipping; /* this vsync's wind-up is skipped */
static int charging, phase_now, actor_now, action_now;
static unsigned judged; /* the vsync judged last */
/* Counters (RPC skip_charging; reset there). */
static unsigned sequences, vsyncs, skipped, hidden_vsyncs, presented_charging;
static unsigned phase_vsyncs[3], phase_presented[3]; /* vsyncs in _cameraMode 4, 5, 6 (presented) */
static long long phase_ns[3]; /* wall time from each such vsync to the next */
/* The name window (RPC skip_charging name): times it opened, presented
 * vsyncs it was up (all, and since the last release), whether it was held
 * past a release, and each opening's action (-1 outside a wind-up) and text
 * (hex, to its terminator 0xE7): "action:text ...". */
static unsigned name_opens, name_vsyncs, name_after, name_held;
static int name_up, names_len;
static char names[2048];
static long long charge_ns;
static struct timespec last;
static int last_actor = -1, last_action = -1;
/* The view at the last release's first vsync (pc_charge_skip_view), and
 * the vsyncs from the last release to its action's end (BATTLE's state
 * leaving 2). */
static int release[9], releases, release_counting;
static unsigned release_vsyncs;
/* The random number pin (tests: RPC skip_charging pin): when an action ends
 * (BATTLE's state leaves 2), the random numbers are seeded from pin_seed and
 * the count of actions ended, so what follows rolls the same whether the
 * wind-up ran or was skipped (it takes fewer vsyncs, each of which draws). */
static int pin_on, pin_seed, pin_state = -1;
static unsigned pins;
/* The actions that ended (tests: who acted, what, whether it wound up,
 * whether a chain / defence ability window opened and where: its tick
 * counted from the release, battleAbilityTargetWindow):
 * "actor:action:wound up:window:its tick ...". */
static char ended[2048];
static int ended_len, window_seen, window_at, wound_up, acting_actor = -1, acting_action = -1;
/* The chain / defence attempts (tests: the input timing around an action;
 * charge_skip.inc pc_charge_skip_ability), when one is spent:
 * "actor:action:timer:window:success ...". */
static char presses[1024];
static int presses_len, attempted_was;
void pc_charge_skip_pin(int on, int seed)
{
    pin_on = on, pin_seed = seed, pins = 0;
}

/* Once per game vsync, before its pacing (boot_runtime.c pc_tick). */
void pc_charge_skip_vsync(void)
{
    unsigned v = pc_vsync_count();
    if (v == judged)
        return;
    judged = v;
    int was = charging, was_skipping = skipping, was_phase = phase_now;
    int phase = 0, skip = 0, actor = -1, action = -1;
    charging = pc_charge_skip_state && pc_charge_skip_state(&phase, &skip, &actor, &action);
    skipping = charging && skip;
    phase_now = charging ? phase : 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (charging) {
        if (!was) {
            ++sequences;
            last_actor = actor, last_action = action;
            trace_len = 0;
        }
        if (pc_charge_skip_trace && trace_len < (int)sizeof trace - 256) {
            trace_len += pc_charge_skip_trace(trace + trace_len, (int)sizeof trace - trace_len - 2);
            /* (and the wall time since the vsync before, ms) */
            trace_len += snprintf(trace + trace_len, sizeof trace - (size_t)trace_len, "@%.1f",
                was ? ((now.tv_sec - last.tv_sec) * 1e9 + (now.tv_nsec - last.tv_nsec)) / 1e6 : 0.0);
            trace[trace_len++] = ' ';
            trace[trace_len] = 0;
        }
        ++vsyncs;
        skipped += skipping != 0;
        if (phase >= 4 && phase <= 6)
            ++phase_vsyncs[phase - 4];
    }
    if (was && !charging && pc_charge_skip_view) {
        pc_charge_skip_view(release, last_actor); /* (the release's first vsync) */
        ++releases;
    }
    if (pc_charge_skip_battle_state) {
        int camera, who, what, state = pc_charge_skip_battle_state(&camera, &who, &what);
        if (was && !charging)
            release_counting = 1, release_vsyncs = 0;
        if (state != 2)
            release_counting = 0;
        release_vsyncs += release_counting != 0;
        if (state == 2 && what > 0)
            acting_actor = who, acting_action = what;
        if (state == 2 && pc_charge_skip_window && pc_charge_skip_window())
            window_seen = 1, window_at = pc_charge_skip_window();
        int ability[4];
        if (state == 2 && pc_charge_skip_ability && pc_charge_skip_ability(ability)) {
            if (ability[2] && !attempted_was && presses_len < (int)sizeof presses - 48)
                presses_len += snprintf(presses + presses_len, sizeof presses - (size_t)presses_len,
                    "%s%d:%d:%d:%d:%d", presses_len ? " " : "", who, what, ability[0], ability[1], ability[3]);
            attempted_was = ability[2];
        } else
            attempted_was = 0;
        wound_up |= charging;
        if (pin_state == 2 && state != 2) {
            if (ended_len < (int)sizeof ended - 32)
                ended_len += snprintf(ended + ended_len, sizeof ended - (size_t)ended_len, "%s%d:%d:%d:%d:%d",
                    ended_len ? " " : "", acting_actor, acting_action, wound_up, window_seen, window_at);
            window_seen = window_at = wound_up = 0, acting_actor = acting_action = -1;
            if (pin_on && pc_charge_skip_reseed)
                pc_charge_skip_reseed((unsigned)pin_seed + ++pins);
        }
        pin_state = state;
    }
    if (pc_charge_skip_name_window) {
        unsigned char text[112];
        int held, n = pc_charge_skip_name_window(text, (int)sizeof text, &held);
        if (n >= 0 && !name_up) {
            ++name_opens;
            if (names_len < (int)sizeof names - 2 * n - 16) {
                names_len += snprintf(names + names_len, sizeof names - (size_t)names_len, "%s%d:",
                    names_len ? " " : "", charging ? action : -1);
                for (int i = 0; i < n && !(i >= 4 && text[i] == 0xE7); ++i)
                    names_len += snprintf(names + names_len, 3, "%02x", text[i]);
            }
        }
        name_up = n >= 0;
        name_held |= name_up && held;
    }
    if (was && !charging)
        name_after = 0;
    if (was) { /* from the wind-up's first vsync to the release's first */
        long long ns = (now.tv_sec - last.tv_sec) * 1000000000LL + (now.tv_nsec - last.tv_nsec);
        charge_ns += ns;
        if (was_phase >= 4 && was_phase <= 6)
            phase_ns[was_phase - 4] += ns;
    }
    last = now;
    /* The game double-buffers: a vsync shows what the one before it drew.
     * So the vsync after a skipped one is hidden too (it would show the
     * wind-up's last frame); the next present is the release's own. */
    int hide = skipping || was_skipping;
    if (hidden && !hide && &pc_hw_history_cut)
        ++pc_hw_history_cut; /* the temporal filters restart from this frame */
    hidden = hide;
    hidden_vsyncs += hidden != 0;
    presented_charging += charging && !hidden;
    if (charging && !hidden && phase >= 4 && phase <= 6)
        ++phase_presented[phase - 4];
    name_vsyncs += name_up && !hidden;
    name_after += name_up && !hidden && !charging;
}
/* Whether this vsync is presented (boot_runtime.c) and paced (boot_window.c). */
int pc_charge_skip_hidden(void) { return hidden; }

/* RPC skip_charging (dev_rpc.c). */
void pc_charge_skip_stats(char* out, int size, int reset)
{
    snprintf(out, (size_t)size,
        "{\"mode\":%d,\"who\":%d,\"charging\":%d,\"phase\":%d,\"skipping\":%d,\"hidden\":%d,"
        "\"sequences\":%u,\"vsyncs\":%u,\"skipped\":%u,\"hidden_vsyncs\":%u,\"presented\":%u,"
        "\"hold_mine\":%.2f,\"hold_enemy\":%.2f,"
        "\"phases\":[%u,%u,%u],\"phases_presented\":[%u,%u,%u],\"phases_ms\":[%.1f,%.1f,%.1f],\"ms\":%.1f,\"actor\":%d,\"action\":%d,\"history_cut\":%u,"
        "\"trace\":\"%s\",\"releases\":%d,\"release_vsyncs\":%u,\"release\":[%d,%d,%d,%d,%d,%d,%d,%d,%d],\"pin\":%d,\"pins\":%u,\"pump\":%d,\"pumped\":%d,\"ended\":\"%s\",\"presses\":\"%s\","
        "\"name_seconds\":%.2f,\"name\":{\"opens\":%u,\"vsyncs\":%u,\"after\":%u,\"held\":%u,\"up\":%d,"
        "\"names\":\"%s\"}}",
        pc_qol_skip_charging, pc_qol_skip_charging_who, charging, phase_now, skipping, hidden, sequences, vsyncs,
        skipped, hidden_vsyncs, presented_charging, pc_qol_skip_charging_hold_mine, pc_qol_skip_charging_hold_enemy,
        phase_vsyncs[0], phase_vsyncs[1], phase_vsyncs[2],
        phase_presented[0], phase_presented[1], phase_presented[2],
        phase_ns[0] / 1e6, phase_ns[1] / 1e6, phase_ns[2] / 1e6, charge_ns / 1e6, last_actor, last_action, &pc_hw_history_cut ? pc_hw_history_cut : 0, trace, releases, release_vsyncs,
        release[0], release[1], release[2], release[3], release[4], release[5], release[6], release[7], release[8],
        pin_on ? pin_seed : -1, pins, &pc_charge_skip_pump ? pc_charge_skip_pump : -1,
        &pc_charge_skip_pumped ? pc_charge_skip_pumped : -1, ended, presses, pc_qol_skip_charging_name, name_opens, name_vsyncs,
        name_after, name_held, name_up, names);
    if (reset) {
        sequences = vsyncs = skipped = hidden_vsyncs = presented_charging = 0;
        phase_vsyncs[0] = phase_vsyncs[1] = phase_vsyncs[2] = 0;
        phase_presented[0] = phase_presented[1] = phase_presented[2] = 0;
        phase_ns[0] = phase_ns[1] = phase_ns[2] = 0;
        name_opens = name_vsyncs = name_after = name_held = 0;
        names_len = 0, names[0] = 0;
        charge_ns = 0;
        last_actor = last_action = -1;
        releases = 0;
        ended_len = 0, ended[0] = 0;
        presses_len = 0, presses[0] = 0;
    }
}
