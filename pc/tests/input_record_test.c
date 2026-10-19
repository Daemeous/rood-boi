/* Standalone: includes the implementation to inject allocation failures.
 * No window, SDL, or game required; pc_vsync_count is a fake clock. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned tick;
unsigned pc_vsync_count(void) { return tick; }
static int fail_allocation;
static void* test_realloc(void* p, size_t size)
{
    if (fail_allocation > 0 && --fail_allocation == 0)
        return NULL;
    return realloc(p, size);
}
#define realloc test_realloc
#include "../src/input_record.c"
#undef realloc
static int rpc_valid;
static float rpc_stick[2];
int pc_rpc_stick(float out[2])
{
    out[0] = rpc_stick[0]; out[1] = rpc_stick[1];
    return rpc_valid;
}

static char recording[128], bad[128];
static void fixture(const char* text)
{
    FILE* f = fopen(bad, "w");
    assert(f);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}
static int state(void)
{
    const char* file;
    unsigned end;
    return pc_input_record_state(&file, &end);
}
static unsigned buttons(unsigned t) { return (t * 17u) & 0xffff; }
static float stick(unsigned t) { return ((int)(t % 513) - 256) / 256.0f; }
static void check_tick(unsigned t)
{
    float out[2] = { 1, 1 };
    tick = t;
    /* Alternate polling order, since either device advances the replay. */
    if (t & 1)
        assert(pc_input_buttons(~0u, ~0u) == buttons(t));
    assert(pc_input_stick(out, 0) == 1);
    assert(out[0] == stick(t) && out[1] == -stick(t));
    assert(pc_input_buttons(~0u, ~0u) == buttons(t));
}

int main(void)
{
    char dir[] = "/tmp/rood-input-test-XXXXXX";
    assert(mkdtemp(dir));
    snprintf(recording, sizeof recording, "%s/route.rec", dir);
    snprintf(bad, sizeof bad, "%s/bad.rec", dir);
    unsetenv("ROOD_RECORD");
    unsetenv("ROOD_REPLAY");
    assert(state() == 0);
    assert(pc_input_record_rpc(NULL) == 0);
    assert(pc_input_replay_rpc(NULL) == 0);
    assert(pc_input_record_rpc(recording) == 0);
    assert(pc_input_replay_rpc(NULL) == -1);
    assert(pc_input_replay_rpc(recording) == -1);
    assert(state() == 1);
    for (tick = 0; tick < 600; tick++) {
        float out[2] = { stick(tick), -stick(tick) };
        unsigned b = buttons(tick);
        assert(pc_input_buttons(b & 0xff, b & ~0xffu) == b);
        assert(pc_input_stick(out, 1) == 1);
    }
    assert(pc_input_record_rpc("") == 0);
    tick = 0;
    assert(pc_input_replay_rpc(recording) == 0);
    assert(pc_input_record_rpc(NULL) == -1);
    assert(pc_input_record_rpc(recording) == -1);
    for (unsigned t = 0; t < 600; t++)
        check_tick(t);
    tick = 600;
    assert(pc_input_buttons(1, 2) == 3);
    assert(state() == 3 && pc_input_deterministic());
    tick = 0;
    pc_input_rewound();
    for (unsigned t = 0; t < 600; t++)
        check_tick(t);
    tick = 123;
    pc_input_rewound();
    check_tick(123);
    check_tick(456);

    /* Failed replacements leave the current replay and its cursor intact. */
    const char* invalid[] = {
        "", "# no events\n", "garbage\n", "B -1 1\n", "B 4294967296 1\n",
        "B 0 100000000\n", "B 0 xyz\n", "B 0 1 junk\n", "S 0 257 0\n",
        "S 0 0 -257\n", "B 2 1\nB 1 2\n", "B 2 1\nE 1\n",
        "E 3\nB 3 1\n", "E 1\nE 2\n", "B 4294967295 1\n",
        "B 99999999999999999999999999999 1\n", "S 0 0\n"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        fixture(invalid[i]);
        assert(pc_input_replay_rpc(bad) == -1);
        assert(state() == 2);
        check_tick(456);
    }
    char long_path[1100];
    memset(long_path, 'x', sizeof long_path - 1);
    long_path[sizeof long_path - 1] = 0;
    assert(pc_input_replay_rpc(long_path) == -1);
    fixture(long_path);
    assert(pc_input_replay_rpc(bad) == -1);
    fail_allocation = 1;
    assert(pc_input_replay_rpc(recording) == -1);
    fail_allocation = 2; /* fail growth after the first 1024 events */
    assert(pc_input_replay_rpc(recording) == -1);
    fail_allocation = 0;
    check_tick(457);
    assert(pc_input_replay_rpc(NULL) == 0);
    assert(!pc_input_deterministic());
    assert(pc_input_record_rpc(long_path) == -1);
    assert(state() == 0);
    assert(pc_input_replay_rpc("/nonexistent/rood-input-test/route.rec") == -1);
    assert(pc_input_record_rpc("/nonexistent/rood-input-test/route.rec") == -1);
    assert(state() == 0);

    /* Recording rewind closes the file with a monotonic end, then goes live. */
    tick = 100;
    assert(pc_input_record_rpc(bad) == 0);
    assert(pc_input_buttons(~0u, 0) == ~0u);
    tick = 200;
    assert(pc_input_buttons(7, 0) == 7);
    tick = 50;
    pc_input_rewound();
    assert(state() == 0);
    assert(pc_input_buttons(9, 0) == 9);
    assert(pc_input_replay_rpc(bad) == 0);
    assert(pc_input_buttons(9, 0) == 0);
    tick = 100;
    assert(pc_input_buttons(9, 0) == ~0u);
    tick = 200;
    assert(pc_input_buttons(9, 0) == 7);
    tick = 201;
    assert(pc_input_buttons(9, 0) == 9);
    assert(pc_input_replay_rpc("") == 0);

    /* A flushed, unterminated recording remains usable; E 0 is not missing E. */
    fixture("B 10 ff\nS 10 -256 256\n");
    tick = 10;
    assert(pc_input_replay_rpc(bad) == 0);
    assert(pc_input_buttons(0, 0) == 255);
    tick = 11;
    assert(pc_input_buttons(1, 0) == 1);
    assert(pc_input_replay_rpc(NULL) == 0);
    fixture("E 0\n");
    tick = 0;
    assert(pc_input_replay_rpc(bad) == 0);
    assert(pc_input_buttons(4, 0) == 4);
    assert(pc_input_replay_rpc(NULL) == 0);
    /* Combined keyboard/controller/RPC values are clamped before recording;
     * replay replaces every live source, including RPC stick injections. */
    tick = 300;
    rpc_valid = 1; rpc_stick[0] = 0.75f; rpc_stick[1] = -0.75f;
    assert(pc_input_record_rpc(recording) == 0);
    float combined[2] = {0.75f, -0.75f};
    assert(pc_input_stick(combined, 1) == 1);
    assert(combined[0] == 1 && combined[1] == -1);
    tick++;
    assert(pc_input_record_rpc(NULL) == 0);
    tick = 300;
    rpc_stick[0] = -1; rpc_stick[1] = 1;
    assert(pc_input_replay_rpc(recording) == 0);
    combined[0] = combined[1] = 0;
    assert(pc_input_stick(combined, 0) == 1);
    assert(combined[0] == 1 && combined[1] == -1);
    assert(pc_input_replay_rpc(NULL) == 0);
    assert(unlink(recording) == 0);
    assert(unlink(bad) == 0);
    assert(rmdir(dir) == 0);
    puts("input_record_test: PASS (600 ticks, rewind, transitions, malformed files, allocation failure)");
    return 0;
}
