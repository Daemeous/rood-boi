/* Small host-platform differences shared by the PC layer (Linux, Windows). */
#ifndef ROOD_PLATFORM_H
#define ROOD_PLATFORM_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define pc_mkdir(path) _mkdir(path)
/* Modification time in ns (Windows' stat has whole seconds). */
#define PC_MTIME_NS(st) ((long long)(st).st_mtime * 1000000000LL)
#else
#define pc_mkdir(path) mkdir(path, 0777)
#define PC_MTIME_NS(st) ((long long)(st).st_mtim.tv_sec * 1000000000LL + (st).st_mtim.tv_nsec)
#endif

/* Puts `temp` in place of `path` (replacing it) in one step: rename on
 * POSIX; on Windows MoveFileEx (rename there fails when the target exists).
 * 0 on success; else -1 with errno set, and `path` untouched. Windows never
 * removes `path` first: a move can fail while a virus scanner holds the
 * fresh temp file for a moment, and removing the old file then would lose
 * the save (a memory card). Such a move is retried for up to ~0.2 s. */
static inline int pc_replace_file(const char* temp, const char* path)
{
#ifdef _WIN32
    for (int attempt = 0;; attempt++) {
        if (MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return 0;
        DWORD error = GetLastError();
        int busy = error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION
                || error == ERROR_LOCK_VIOLATION;
        if (!busy || attempt == 10) {
            errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT
                  : busy                                                         ? EACCES
                                                                                 : EIO;
            return -1;
        }
        Sleep(20);
    }
#else
    return rename(temp, path);
#endif
}

/* Calls write(f) and returns what it wrote, malloc'd (*size bytes, plus a
 * NUL), or NULL. */
static inline char* pc_capture_file(void (*write)(FILE*), size_t* size)
{
#ifdef _WIN32
    FILE* f = rood_tmpfile();
    if (!f)
        return NULL;
    write(f);
    long n = ftell(f);
    char* buffer = (char*)malloc(n > 0 ? n + 1 : 1);
    rewind(f);
    *size = n > 0 && buffer ? fread(buffer, 1, n, f) : 0;
    if (buffer)
        buffer[*size] = 0;
    fclose(f);
    return buffer;
#else
    char* buffer = NULL;
    FILE* f = open_memstream(&buffer, size);
    if (!f)
        return NULL;
    write(f);
    fclose(f);
    return buffer;
#endif
}
#endif
