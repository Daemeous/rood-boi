/* Small host-platform differences shared by the PC layer (Linux, Windows). */
#ifndef ROOD_PLATFORM_H
#define ROOD_PLATFORM_H
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

/* Puts `temp` in place of `path` (replacing it), as one step where the
 * system allows: rename on POSIX; on Windows MoveFileEx (rename there fails
 * when the target exists), else remove and rename. 0 on success. */
static inline int pc_replace_file(const char* temp, const char* path)
{
#ifdef _WIN32
    if (MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 0;
    remove(path);
#endif
    return rename(temp, path);
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
