/* pc_replace_file (platform.h): a save written as NAME.tmp replaces NAME
 * whether NAME exists or not, again and again. Windows' rename() fails when
 * the target exists, so there a card saved once never changed again; build
 * with MinGW and run under Wine to cover that (build_boot.py --target
 * windows does when Wine is installed). */
#include "platform.h"
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int write_text(const char* path, const char* text)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}

static int has_text(const char* path, const char* text)
{
    char buffer[64] = "";
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buffer, 1, sizeof buffer - 1, f);
    fclose(f);
    buffer[n] = 0;
    return !strcmp(buffer, text);
}

int main(int argc, char** argv)
{
    char path[512], temp[520];
    snprintf(path, sizeof path, "%s/rood-replace-probe.bin", argc > 1 ? argv[1] : ".");
    snprintf(temp, sizeof temp, "%s.tmp", path);
    remove(path);
    remove(temp);
    static const char* saves[] = { "first save", "second save", "third" };
    for (int i = 0; i < 3; i++) {
        CHECK(write_text(temp, saves[i]));
        CHECK(pc_replace_file(temp, path) == 0);
        CHECK(has_text(path, saves[i]));
        FILE* left = fopen(temp, "rb");
        CHECK(!left);
    }
    /* A missing source fails and leaves the target alone. */
    CHECK(pc_replace_file(temp, path) != 0);
    CHECK(has_text(path, saves[2]));
    remove(path);
    puts("PASS: pc_replace_file replaces an existing file (three saves in a row)");
    return 0;
}
