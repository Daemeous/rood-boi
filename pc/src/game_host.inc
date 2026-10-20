/* Owned-code reload service, included by the A5 host. No engine-owned state.
 * The game thread alone calls routed code. Slot publication is a transaction
 * at its vsync boundary: no callbacks or game execution inside the commit. */
extern void* pc_owned_slots[] __attribute__((weak));
extern void** pc_owned_table __attribute__((weak));
extern const char pc_owned_abi[] __attribute__((weak));
extern const unsigned pc_route_count __attribute__((weak));
extern const char* const pc_route_names[] __attribute__((weak));
extern const char* const pc_route_signatures[] __attribute__((weak));
static void* game_handle;
static char game_library[1024], game_live[1100], game_status[1024];
static unsigned game_generation, game_serial;
static int game_pending, game_ok;
static uintptr_t game_lo, game_hi;

int pc_game_reload_request(void)
{
    if (!game_handle) return 0;
    game_pending = 1;
    return 1;
}
int pc_game_reload_pending(void) { return game_pending; }
int pc_game_reload_status(char* out, int size)
{
    snprintf(out, (size_t)size, "%s", game_status);
    return game_ok;
}
struct game_range { const char* path; uintptr_t lo, hi; };
static int game_range_cb(struct dl_phdr_info* info, size_t size, void* data)
{
    (void)size;
    struct game_range* r = data;
    if (!info->dlpi_name || strcmp(info->dlpi_name, r->path)) return 0;
    r->lo = UINTPTR_MAX;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD) continue;
        uintptr_t lo = info->dlpi_addr + p->p_vaddr, hi = lo + p->p_memsz;
        if (lo < r->lo) r->lo = lo;
        if (hi > r->hi) r->hi = hi;
    }
    return 1;
}
struct game_frames { int live, complete, count; };
static _Unwind_Reason_Code game_frame(struct _Unwind_Context* ctx, void* raw)
{
    struct game_frames* f = raw;
    uintptr_t ip = _Unwind_GetIP(ctx);
    if (ip >= game_lo && ip < game_hi) f->live = 1;
    if (_Unwind_GetRegionStart(ctx) == (uintptr_t)vs_main_exec) {
        f->complete = 1;
        return _URC_END_OF_STACK;
    }
    return ++f->count < 4096 ? _URC_NO_REASON : _URC_END_OF_STACK;
}
/* A saved/rewound stack must not retain return addresses into an unloadable
 * generation either. The weak callers leave static builds unchanged. */
int pc_game_snapshot_safe(void)
{
    if (!game_handle) return 1;
    struct game_frames frames = { 0, 0, 0 };
    _Unwind_Backtrace(game_frame, &frames);
    return frames.complete && !frames.live;
}
static void game_remove_live(void)
{
    if (game_live[0]) unlink(game_live);
}
static int game_load(void)
{
    char candidate[1100];
    snprintf(candidate, sizeof candidate, "%s.live.%ld.%u", game_library, (long)getpid(), ++game_serial);
    if (link(game_library, candidate)) {
        snprintf(game_status, sizeof game_status, "Zig library link: %s", strerror(errno));
        return 0;
    }
    void* h = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        snprintf(game_status, sizeof game_status, "Zig library load: %s", dlerror());
        unlink(candidate);
        return 0;
    }
    void** slots = calloc(pc_route_count, sizeof *slots);
    const char* key = dlsym(h, "rood_game_abi");
    int valid = slots && key && !strcmp(key, pc_owned_abi);
    snprintf(game_status, sizeof game_status, "Zig library ABI differs: rebuild host and restart");
    for (unsigned i = 0; valid && i < pc_route_count; i++) {
        char name[256];
        snprintf(name, sizeof name, "%s__candidate", pc_route_names[i]);
        slots[i] = dlsym(h, name);
        if (!slots[i]) {
            valid = 0;
            snprintf(game_status, sizeof game_status, "Zig library missing %s", name);
        } else {
            snprintf(name, sizeof name, "%s__candidate_abi", pc_route_names[i]);
            const char* signature = dlsym(h, name);
            if (!signature || strcmp(signature, pc_route_signatures[i])) {
                valid = 0;
                snprintf(game_status, sizeof game_status, "Zig library missing or incompatible signature for %s (rebuild host)", pc_route_names[i]);
            }
        }
    }
    struct link_map* map = NULL;
    dlinfo(h, RTLD_DI_LINKMAP, &map);
    /* dlopen reuses a handle when an unchanged inode is requested again. */
    struct game_range range = { map ? map->l_name : candidate, 0, 0 };
    dl_iterate_phdr(game_range_cb, &range);
    if (!range.lo || !range.hi) valid = 0;
    if (!valid) {
        free(slots);
        dlclose(h);
        unlink(candidate);
        return 0;
    }
    /* No game execution between validation and publication. Old code remains
     * mapped until every new slot is installed. Failure never touches slots. */
    void* previous = game_handle;
    /* One aligned pointer publication, not a sequence of partially visible
     * slots. x86-64 trampoline loads have acquire ordering. Only this game
     * thread executes owned code, so no reader can retain the retired table. */
    void** retired = __atomic_exchange_n(&pc_owned_table, slots, __ATOMIC_ACQ_REL);
    game_handle = h;
    game_lo = range.lo;
    game_hi = range.hi;
    game_generation++;
    if (previous) {
        dlclose(previous);
        free(retired);
    }
    game_remove_live();
    snprintf(game_live, sizeof game_live, "%s", candidate);
    return 1;
}
static void pc_game_start(void)
{
    if (!pc_owned_slots) return; /* no-Zig host */
    const char* path = getenv("ROOD_GAME_LIBRARY");
    if (path && path[0]) snprintf(game_library, sizeof game_library, "%s", path);
    else {
        ssize_t n = readlink("/proc/self/exe", game_library, sizeof game_library - 1);
        if (n < 0) abort();
        game_library[n] = 0;
        char* slash = strrchr(game_library, '/');
        if (!slash) abort();
        snprintf(slash + 1, sizeof game_library - (size_t)(slash + 1 - game_library), "librood_game.so");
    }
    if (!game_load()) {
        fprintf(stderr, "[zig] %s\n", game_status);
        exit(1);
    }
    atexit(game_remove_live);
    fprintf(stderr, "[zig] loaded generation %u (%u routes, Debug)\n", game_generation, pc_route_count);
}
static void pc_game_vsync(void)
{
    while (game_pending) {
        game_pending = 0;
        struct game_frames frames = { 0, 0, 0 };
        _Unwind_Backtrace(game_frame, &frames);
        game_ok = 0;
        if (frames.live || !frames.complete)
            snprintf(game_status, sizeof game_status, "reload refused (retry: owned frame live or incomplete unwind)");
        else {
            struct timespec before, after;
            clock_gettime(CLOCK_MONOTONIC, &before);
            game_ok = game_load();
            clock_gettime(CLOCK_MONOTONIC, &after);
            if (game_ok)
                snprintf(game_status, sizeof game_status,
                    "{\"reloaded\":true,\"vsync\":%u,\"generation\":%u,\"ms\":%.3f,\"target\":\"zig\"}",
                    pc_vsync_count(), game_generation,
                    (after.tv_sec - before.tv_sec) * 1000.0 + (after.tv_nsec - before.tv_nsec) / 1e6);
        }
        fprintf(stderr, "[zig] %s\n", game_status);
        pc_rpc_reloaded();
    }
}
