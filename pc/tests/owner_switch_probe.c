/* Host owner API, including environment defaults and unavailable shadow. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint32_t rood_game_abi = 1;
const unsigned pc_route_count = 3;
const unsigned char pc_route_subsystems[] = { 0, 1, 1 };
const unsigned pc_subsystem_count = 2;
const char* const pc_subsystem_names[] = { "alpha", "beta" };
const unsigned char pc_subsystem_defaults[] = { 0, 1 };
static int shadow_ready;
int pc_shadow_available(void) { return shadow_ready; }
void pc_unimplemented(const char* what) { (void)what; abort(); }
int pc_owner_routes(int);
int pc_owner_count(void);
const char* pc_owner_subsystem(int);
const char* pc_owner_label(int);
int pc_owner_get(int);
int pc_owner_find(const char*);
int pc_owner_parse(const char*);
int pc_owner_set(int, int);

int main(int argc, char** argv)
{
    assert(pc_owner_count() == 2);
    assert(pc_owner_routes(0) == 1 && pc_owner_routes(1) == 2 && pc_owner_routes(-1) == 0);
    assert(pc_owner_get(0) == (argc > 1 ? atoi(argv[1]) : 0));
    assert(pc_owner_get(1) == 1);
    assert(!strcmp(pc_owner_subsystem(1), "beta"));
    assert(pc_owner_subsystem(-1) == NULL && pc_owner_subsystem(2) == NULL);
    assert(pc_owner_find("alpha") == 0 && pc_owner_find("missing") == -1);
    assert(pc_owner_parse("shadow") == 2 && pc_owner_parse("invalid") == -1);
    assert(!strcmp(pc_owner_label(2), "shadow") && !strcmp(pc_owner_label(3), "?"));
    assert(pc_owner_get(-1) == -1 && pc_owner_get(2) == -1);
    assert(pc_owner_set(-1, 0) == -1 && pc_owner_set(2, 0) == -1);
    assert(pc_owner_set(0, -1) == -1 && pc_owner_set(0, 3) == -1);
    assert(pc_owner_set(0, 2) == -2);
    assert(pc_owner_set(0, 1) == 0 && pc_owner_get(0) == 1);
    shadow_ready = 1;
#ifdef _WIN32
    assert(pc_owner_set(0, 2) == -2);
#else
    assert(pc_owner_set(0, 2) == 0 && pc_owner_get(0) == 2);
#endif
    assert(pc_owner_set(0, 0) == 0 && pc_owner_get(0) == 0);
    puts("owner switch: 15 checks passed");
    return 0;
}
