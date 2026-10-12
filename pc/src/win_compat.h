/* Force-included into every host (pc/src, third-party) object of the Windows
 * build (build_boot.py --target windows), never into the game sources. It
 * includes <windows.h> once, up front, and undoes the macros of it that
 * collide with ordinary identifiers, then fills small POSIX gaps. */
#ifndef ROOD_WIN_COMPAT_H
#define ROOD_WIN_COMPAT_H
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread_time.h>

/* clock_nanosleep: winpthreads sleeps to absolute deadlines on
 * CLOCK_REALTIME only (CLOCK_MONOTONIC returns at once, and the frame loop,
 * paced to 59.94 Hz with it, ran unthrottled). A high-resolution waitable
 * timer gets within half a millisecond, then the clock is polled. */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static inline int rood_clock_nanosleep(clockid_t clock, int flags, const struct timespec* request,
    struct timespec* remain)
{
    struct timespec now;
    (void)remain;
    clock_gettime(clock, &now);
    long long now_ns = (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
    long long target = (long long)request->tv_sec * 1000000000LL + request->tv_nsec;
    if (!(flags & TIMER_ABSTIME))
        target += now_ns;
    long long coarse = target - now_ns - 500000;
    if (coarse > 0) {
        static __thread HANDLE timer;
        if (!timer)
            timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        LARGE_INTEGER due;
        due.QuadPart = -(coarse / 100); /* relative, in 100 ns units */
        if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
            WaitForSingleObject(timer, INFINITE);
        else
            Sleep((DWORD)(coarse / 1000000));
    }
    do {
        clock_gettime(clock, &now);
    } while ((long long)now.tv_sec * 1000000000LL + now.tv_nsec < target);
    return 0;
}
#define clock_nanosleep rood_clock_nanosleep

/* GNU strcasestr (editor search boxes). */
static inline char* rood_strcasestr(const char* haystack, const char* needle)
{
    size_t n = strlen(needle);
    for (; *haystack; haystack++)
        if (!_strnicmp(haystack, needle, n))
            return (char*)haystack;
    return n ? NULL : (char*)haystack;
}
#define strcasestr rood_strcasestr

/* A scratch file deleted when closed, in %TEMP% (the CRT's tmpfile() uses
 * the drive root, which is often not writable). */
static inline FILE* rood_tmpfile(void)
{
    char dir[MAX_PATH], path[MAX_PATH];
    if (!GetTempPathA(sizeof dir, dir) || !GetTempFileNameA(dir, "rood", 0, path))
        return NULL;
    return fopen(path, "w+bD");
}

#endif /* _WIN32 */
#endif
