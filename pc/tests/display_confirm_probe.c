/* "Keep these settings?" (pc/src/display_confirm.c) against a fake clock and
 * settings store: keep, timeout, cancel, a second change while pending,
 * nothing unconfirmed saved, a revert that reaches back into begin. */
#include "display_confirm.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* The settings: live values, and what the last save wrote (as pc_settings_write
 * would: a held key's snapshot instead of its live value). */
enum { KEYS = 4 };
static const char* const names[KEYS] = {"window_mode", "fullscreen_mode", "display", "widescreen"};
static char live[KEYS][96], saved[KEYS][96];
static double clock_now;
static int sets, saves, reenter;

static int key_index(const char* key)
{
    for (int i = 0; i < KEYS; i++)
        if (!strcmp(names[i], key))
            return i;
    return -1;
}
static double fake_now(void* user)
{
    (void)user;
    return clock_now;
}
static int fake_get(void* user, const char* key, char* out, int size)
{
    (void)user;
    int i = key_index(key);
    if (i < 0)
        return 0;
    snprintf(out, (size_t)size, "%s", live[i]);
    return 1;
}
static void fake_set(void* user, const char* key, const char* value)
{
    (void)user;
    int i = key_index(key);
    if (i >= 0)
        snprintf(live[i], sizeof live[i], "%s", value);
    ++sets;
    if (reenter) /* a back-end that routes every write through the confirm */
        pc_display_confirm_begin(key, value);
}
static void write_settings(void)
{
    for (int i = 0; i < KEYS; i++)
        if (!pc_display_confirm_held(names[i], saved[i], (int)sizeof saved[i]))
            snprintf(saved[i], sizeof saved[i], "%s", live[i]);
}
static void fake_save(void* user)
{
    (void)user;
    ++saves;
    write_settings();
}

static void reset(void)
{
    const char* start[KEYS] = {"windowed", "desktop", "0 DP-1", "1"};
    for (int i = 0; i < KEYS; i++) {
        snprintf(live[i], sizeof live[i], "%s", start[i]);
        snprintf(saved[i], sizeof saved[i], "%s", start[i]);
    }
    clock_now = 100.0;
    sets = saves = reenter = 0;
    pc_display_confirm_ops ops = {fake_now, fake_get, fake_set, fake_save, NULL};
    pc_display_confirm_init(&ops);
    pc_display_confirm_set_timeout(0); /* the default */
}

/* A held key set back to its snapshot lets it go; the last one ends the confirm. */
static void back_again(void)
{
    reset();
    CHECK(pc_display_confirm_begin("window_mode", "fullscreen") == 1);
    CHECK(pc_display_confirm_begin("display", "1 HDMI-1") == 1);
    clock_now += 5.0;
    CHECK(pc_display_confirm_begin("window_mode", "windowed") == 1); /* display still waits */
    CHECK(!strcmp(live[0], "windowed") && pc_display_confirm_count() == 1);
    CHECK(!strcmp(pc_display_confirm_key(0), "display") && pc_display_confirm_remaining() > 9.99);
    CHECK(pc_display_confirm_begin("display", "0 DP-1") == 0); /* the last: over, nothing to keep */
    CHECK(!pc_display_confirm_pending() && !strcmp(live[2], "0 DP-1") && saves == 0);
    clock_now += 20.0;
    CHECK(!pc_display_confirm_tick());
}

static void keep(void)
{
    reset();
    CHECK(pc_display_confirm_timeout() == 10.0);
    CHECK(!pc_display_confirm_pending());
    CHECK(pc_display_confirm_begin("window_mode", "fullscreen") == 1);
    CHECK(pc_display_confirm_pending());
    CHECK(!strcmp(live[0], "fullscreen")); /* applied at once */
    CHECK(pc_display_confirm_count() == 1 && !strcmp(pc_display_confirm_key(0), "window_mode"));
    clock_now += 4.0;
    CHECK(pc_display_confirm_remaining() > 5.99 && pc_display_confirm_remaining() < 6.01);
    CHECK(!pc_display_confirm_tick());
    pc_display_confirm_keep();
    CHECK(!pc_display_confirm_pending());
    CHECK(!strcmp(live[0], "fullscreen"));
    CHECK(saves == 1 && !strcmp(saved[0], "fullscreen")); /* kept: saved now */
    clock_now += 60.0;
    CHECK(!pc_display_confirm_tick()); /* nothing left to revert */
    CHECK(!strcmp(live[0], "fullscreen"));
    CHECK(pc_display_confirm_remaining() == 0);
}

static void timeout(void)
{
    reset();
    CHECK(pc_display_confirm_begin("fullscreen_mode", "1920x1080@144") == 1);
    clock_now += 9.9;
    CHECK(!pc_display_confirm_tick());
    CHECK(!strcmp(live[1], "1920x1080@144"));
    clock_now += 0.1;
    CHECK(pc_display_confirm_tick() == 1);
    CHECK(!pc_display_confirm_pending());
    CHECK(!strcmp(live[1], "desktop")); /* back */
    CHECK(saves == 0);
    /* a longer countdown (the caller's data) */
    pc_display_confirm_set_timeout(15.0);
    CHECK(pc_display_confirm_begin("fullscreen_mode", "1280x720@60") == 1);
    clock_now += 12.0;
    CHECK(!pc_display_confirm_tick());
    clock_now += 3.0;
    CHECK(pc_display_confirm_tick() == 1 && !strcmp(live[1], "desktop"));
}

static void cancel(void)
{
    reset();
    CHECK(pc_display_confirm_begin("display", "1 HDMI-1") == 1);
    clock_now += 2.0;
    pc_display_confirm_revert(); /* Cross */
    CHECK(!pc_display_confirm_pending());
    CHECK(!strcmp(live[2], "0 DP-1"));
    CHECK(saves == 0);
    pc_display_confirm_revert(); /* twice: nothing more */
    pc_display_confirm_keep();
    CHECK(saves == 0 && !strcmp(live[2], "0 DP-1"));
}

static void second_change(void)
{
    reset();
    CHECK(pc_display_confirm_begin("window_mode", "fullscreen") == 1);
    clock_now += 8.0;
    /* another key joins; the countdown starts again */
    CHECK(pc_display_confirm_begin("fullscreen_mode", "2560x1440@165") == 1);
    CHECK(pc_display_confirm_count() == 2);
    CHECK(pc_display_confirm_remaining() > 9.99);
    clock_now += 8.0;
    CHECK(!pc_display_confirm_tick()); /* 16 s after the first, 8 after the second */
    /* the same key again: its first snapshot stays */
    CHECK(pc_display_confirm_begin("window_mode", "borderless") == 1);
    CHECK(pc_display_confirm_count() == 2);
    CHECK(!strcmp(live[0], "borderless"));
    clock_now += 10.0;
    CHECK(pc_display_confirm_tick() == 1);
    CHECK(!strcmp(live[0], "windowed") && !strcmp(live[1], "desktop")); /* both before the first change */
    CHECK(!pc_display_confirm_pending() && pc_display_confirm_count() == 0);

    /* the same value: no confirm */
    CHECK(pc_display_confirm_begin("window_mode", "windowed") == 0);
    CHECK(!pc_display_confirm_pending());
    /* unknown key or value too long: refused */
    CHECK(pc_display_confirm_begin("nonsense", "1") == -1);
    char longv[200];
    memset(longv, 'x', sizeof longv - 1);
    longv[sizeof longv - 1] = 0;
    CHECK(pc_display_confirm_begin("display", longv) == -1);
    CHECK(!pc_display_confirm_pending());
}

static void no_save_while_pending(void)
{
    reset();
    CHECK(pc_display_confirm_begin("window_mode", "fullscreen") == 1);
    /* an unrelated change and a debounced save in between */
    snprintf(live[3], sizeof live[3], "0");
    write_settings();
    CHECK(!strcmp(saved[0], "windowed")); /* the snapshot, not the unconfirmed mode */
    CHECK(!strcmp(saved[3], "0"));         /* other keys save as usual */
    char v[96];
    CHECK(pc_display_confirm_held("window_mode", v, (int)sizeof v) == 1 && !strcmp(v, "windowed"));
    CHECK(pc_display_confirm_held("widescreen", v, (int)sizeof v) == 0);
    pc_display_confirm_keep();
    CHECK(!strcmp(saved[0], "fullscreen"));
    CHECK(pc_display_confirm_held("window_mode", v, (int)sizeof v) == 0);

    /* reverted: a save after it writes the old mode, the unconfirmed one never reached the file */
    reset();
    CHECK(pc_display_confirm_begin("window_mode", "fullscreen") == 1);
    write_settings();
    clock_now += 11.0;
    pc_display_confirm_tick();
    write_settings();
    CHECK(!strcmp(saved[0], "windowed"));
}

static void reentry(void)
{
    reset();
    reenter = 1; /* every set goes through begin again */
    CHECK(pc_display_confirm_begin("window_mode", "fullscreen") == 1);
    CHECK(pc_display_confirm_pending());
    pc_display_confirm_revert();
    CHECK(!pc_display_confirm_pending()); /* the revert's own sets did not start a new confirm */
    CHECK(!strcmp(live[0], "windowed"));
}

static void no_ops(void)
{
    pc_display_confirm_init(NULL);
    CHECK(pc_display_confirm_begin("window_mode", "fullscreen") == -1);
    CHECK(!pc_display_confirm_pending());
    CHECK(pc_display_confirm_tick() == 0);
}

int main(void)
{
    back_again();
    keep();
    timeout();
    cancel();
    second_change();
    no_save_while_pending();
    reentry();
    no_ops();
    if (failures) {
        printf("display_confirm_probe: %d failure(s)\n", failures);
        return 1;
    }
    printf("display_confirm_probe: ok\n");
    return 0;
}
