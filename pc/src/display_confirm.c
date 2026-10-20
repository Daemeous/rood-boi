/* "Keep these settings?" for risky display changes (display_confirm.h). */
#include "display_confirm.h"
#include <stdio.h>
#include <string.h>

#define DEFAULT_TIMEOUT 10.0
#define KEY_SIZE 48

static pc_display_confirm_ops ops;
static int have_ops;
static double timeout = DEFAULT_TIMEOUT;
static struct {
    char key[KEY_SIZE];
    char old[PC_DISPLAY_CONFIRM_VALUE];
} held[PC_DISPLAY_CONFIRM_KEYS];
static int held_count, pending, busy; /* busy: in our own ops.set (apply or revert) */
static double started;

void pc_display_confirm_init(const pc_display_confirm_ops* o)
{
    have_ops = o && o->now && o->get && o->set;
    if (have_ops)
        ops = *o;
    else
        memset(&ops, 0, sizeof ops);
    held_count = pending = busy = 0;
}

void pc_display_confirm_set_timeout(double seconds) { timeout = seconds > 0 ? seconds : DEFAULT_TIMEOUT; }
double pc_display_confirm_timeout(void) { return timeout; }

static int find(const char* key)
{
    for (int i = 0; i < held_count; i++)
        if (!strcmp(held[i].key, key))
            return i;
    return -1;
}

int pc_display_confirm_begin(const char* key, const char* value)
{
    char now[PC_DISPLAY_CONFIRM_VALUE];
    if (!have_ops || !key || !value || strlen(key) >= KEY_SIZE || strlen(value) >= PC_DISPLAY_CONFIRM_VALUE)
        return -1;
    if (busy) /* our own set reached back here (a back-end that routes every write through us) */
        return 0;
    if (!ops.get(ops.user, key, now, (int)sizeof now))
        return -1;
    int i = find(key);
    if (i < 0) {
        if (!strcmp(now, value))
            return 0; /* nothing changes */
        if (held_count >= PC_DISPLAY_CONFIRM_KEYS)
            return -1;
        i = held_count++;
        snprintf(held[i].key, sizeof held[i].key, "%s", key);
        snprintf(held[i].old, sizeof held[i].old, "%s", now);
    } else if (!strcmp(held[i].old, value)) {
        /* back to where it was: nothing of it to confirm any more */
        busy = 1;
        ops.set(ops.user, key, value);
        busy = 0;
        memmove(&held[i], &held[i + 1], (size_t)(held_count - i - 1) * sizeof held[0]);
        if (--held_count == 0) {
            pending = 0;
            return 0;
        }
        started = ops.now(ops.user);
        return 1;
    }
    busy = 1;
    ops.set(ops.user, key, value);
    busy = 0;
    pending = 1;
    started = ops.now(ops.user);
    return 1;
}

int pc_display_confirm_pending(void) { return pending; }

double pc_display_confirm_remaining(void)
{
    if (!pending)
        return 0;
    double left = timeout - (ops.now(ops.user) - started);
    return left > 0 ? left : 0;
}

int pc_display_confirm_count(void) { return pending ? held_count : 0; }
const char* pc_display_confirm_key(int i) { return pending && i >= 0 && i < held_count ? held[i].key : NULL; }

void pc_display_confirm_keep(void)
{
    if (!pending)
        return;
    pending = held_count = 0;
    if (ops.save)
        ops.save(ops.user);
}

void pc_display_confirm_revert(void)
{
    if (!pending)
        return;
    busy = 1;
    for (int i = held_count - 1; i >= 0; i--)
        ops.set(ops.user, held[i].key, held[i].old);
    busy = 0;
    pending = held_count = 0;
}

int pc_display_confirm_tick(void)
{
    if (!pending || ops.now(ops.user) - started < timeout)
        return 0;
    pc_display_confirm_revert();
    return 1;
}

int pc_display_confirm_held(const char* key, char* out, int size)
{
    int i = pending && key ? find(key) : -1;
    if (i < 0)
        return 0;
    if (out && size > 0)
        snprintf(out, (size_t)size, "%s", held[i].old);
    return 1;
}
