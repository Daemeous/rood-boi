/* File-backed memory card: BIOS file calls, directory listing, events and the
 * raw .mcd image they persist; the other card file formats (DexDrive .gme,
 * VGS .mem, read-only PSP/Vita .vmp) and files that are no card at all. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct DIRENTRY {
    char name[20];
    long attr;
    long size;
    struct DIRENTRY* next;
    long head;
    char system[4];
};
long _card_info(long);
long _card_load(long);
long _card_format(long);
long pc_psx_open(const char*, unsigned long);
long pc_psx_read(long, void*, long);
long pc_psx_write(long, void*, long);
long pc_psx_close(long);
long pc_psx_rename(const char*, const char*);
long erase(const char*);
struct DIRENTRY* firstfile(const char*, struct DIRENTRY*);
struct DIRENTRY* nextfile(struct DIRENTRY*);

static unsigned long last_event;
static long last_spec;
static int deliveries;
void pc_deliver_event(unsigned long event, long spec)
{
    last_event = event;
    last_spec = spec;
    ++deliveries;
}

#define CHECK(x)                                                                            \
    do {                                                                                    \
        if (!(x)) {                                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                    \
            return 1;                                                                       \
        }                                                                                   \
    } while (0)

/* A formatted, empty card, as memory_card.c's format() writes it. */
static void blank_card(unsigned char* image)
{
    memset(image, 0, 0x20000);
    image[0] = 'M';
    image[1] = 'C';
    for (int frame = 1; frame < 36; frame++) {
        unsigned char* f = image + frame * 0x80;
        if (frame < 16)
            f[0] = 0xA0;
        else
            f[0] = f[1] = f[2] = f[3] = 0xFF;
        f[8] = f[9] = 0xFF;
    }
    for (int frame = 0; frame < 36; frame++) {
        unsigned char sum = 0;
        for (int i = 0; i < 127; i++)
            sum ^= image[frame * 0x80 + i];
        image[frame * 0x80 + 127] = sum;
    }
    memcpy(image + 63 * 0x80, image, 0x80);
}

static long write_file(const char* path, const void* data, long size)
{
    FILE* f = fopen(path, "wb");
    long done = f ? (long)fwrite(data, 1, size, f) : -1;
    if (f)
        fclose(f);
    return done;
}
static long read_file(const char* path, void* data, long size)
{
    FILE* f = fopen(path, "rb");
    long done = f ? (long)fread(data, 1, size, f) : -1;
    if (f)
        fclose(f);
    return done;
}

/* One card file in its own process (cards load once per process): `header`
 * bytes starting with `magic` in front of a blank card, or with magic NULL a
 * file of `header` bytes that is no card. A writable one gets a 3-block save
 * and must come back in the same format; any other must stay untouched. */
static int format_case(const char* name, const char* magic, int magic_size, long header, int writable)
{
    char path[] = "/tmp/rood-card-format-XXXXXX";
    int temp = mkstemp(path);
    close(temp);
    static unsigned char file[0xF40 + 0x20000], after[sizeof file + 16];
    long size = magic ? header + 0x20000 : header;
    memset(file, 0, sizeof file);
    if (magic) {
        for (long i = 0; i < header; i++)
            file[i] = (unsigned char)(i * 13 + 7); /* comments and other header bytes */
        memcpy(file, magic, magic_size);
        blank_card(file + header);
    }
    CHECK(write_file(path, file, size) == size);

    pid_t child = fork();
    if (child == 0) {
        setenv("ROOD_CARD1", path, 1);
        static unsigned char data[3 * 8192];
        for (unsigned i = 0; i < sizeof data; i++)
            data[i] = i * 5 + 1;
        long fd = pc_psx_open("bu00:BASLUS-01040VAG1", 0x200 | 3 << 16);
        if (!writable) {
            /* Inserted, empty, and every write refused. */
            struct DIRENTRY entry;
            _exit(fd == -1 && _card_info(0x00) == 1 && last_spec == 0x0004
                        && firstfile("bu00:*", &entry) == NULL && _card_format(0x00) == 0
                    ? 0
                    : 1);
        }
        if (fd < 0)
            _exit(2);
        pc_psx_close(fd);
        fd = pc_psx_open("bu00:BASLUS-01040VAG1", 2);
        _exit(pc_psx_write(fd, data, sizeof data) == sizeof data ? 0 : 3);
    }
    int status;
    CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);

    long got = read_file(path, after, sizeof after);
    if (!writable) {
        CHECK(got == size && !memcmp(after, file, size));
    } else {
        unsigned char* image = after + header;
        CHECK(got == size);
        CHECK(image[0] == 'M' && image[0x80] == 0x51 && image[0x100] == 0x52 && image[0x180] == 0x53);
        CHECK(!strcmp((char*)image + 0x8A, "BASLUS-01040VAG1") && image[0x2000] == 1 && image[0x2001] == 6);
        if (header == 0xF40) {
            /* DexDrive: the header mirrors each directory frame's state and next-block bytes, the rest is kept */
            for (int block = 0; block < 15; block++)
                CHECK(after[22 + block] == image[(block + 1) * 0x80]
                    && after[38 + block] == image[(block + 1) * 0x80 + 8]);
            CHECK(!memcmp(after, file, 22) && !memcmp(after + 37, file + 37, 1)
                && !memcmp(after + 53, file + 53, header - 53));
        } else
            CHECK(!memcmp(after, file, header));
    }
    unlink(path);
    printf("PASS: %s\n", name);
    return 0;
}

int main(void)
{
    if (format_case("DexDrive .gme card: saved in place, header kept and its directory copy updated",
            "123-456-STD", 11, 0xF40, 1)
        || format_case("VGS .mem card: saved in place, header kept", "VgsM", 4, 0x40, 1)
        || format_case("PSP/Vita .vmp card: read only, file untouched", "\0PMV", 4, 0x80, 0)
        || format_case("short file that is no card: never written", NULL, 0, 1000, 0)
        || format_case("128 KiB file without a card header: never written", NULL, 0, 0x20000, 0))
        return 1;

    char path[] = "/tmp/rood-card-probe-XXXXXX";
    int temp = mkstemp(path);
    close(temp);
    unlink(path);
    setenv("ROOD_CARD1", path, 1);
    setenv("ROOD_CARD2", "/nonexistent/rood-card2.mcd", 1);

    CHECK(_card_info(0x00) == 1 && last_event == 0xF4000001ul && last_spec == 0x0004);
    CHECK(_card_info(0x10) == 1 && last_spec == 0x0100);
    CHECK(_card_load(0x00) == 1 && last_spec == 0x0004);
    CHECK(pc_psx_open("bu00:BASLUS-01040VAG1", 1) == -1);
    CHECK(pc_psx_open("bu10:BASLUS-01040VAG1", 0x200 | 3 << 16) == -1);

    long fd = pc_psx_open("bu00:BASLUS-01040VAG1", 0x200 | 3 << 16);
    CHECK(fd >= 0 && pc_psx_close(fd) == fd);
    CHECK(pc_psx_open("bu00:BASLUS-01040VAG1", 0x200 | 1 << 16) == -1);

    static unsigned char data[3 * 8192], back[3 * 8192];
    for (unsigned i = 0; i < sizeof data; i++)
        data[i] = i * 7 + (i >> 13);
    fd = pc_psx_open("bu00:BASLUS-01040VAG1", 0x8000 | 2);
    deliveries = 0;
    CHECK(pc_psx_write(fd, data, sizeof data) == sizeof data && deliveries == 1
        && last_spec == 0x0004);
    CHECK(pc_psx_write(fd, data, 128) == 0);
    pc_psx_close(fd);
    fd = pc_psx_open("bu00:BASLUS-01040VAG1", 1);
    deliveries = 0;
    CHECK(pc_psx_read(fd, back, sizeof back) == sizeof back && deliveries == 0);
    CHECK(!memcmp(data, back, sizeof data));
    pc_psx_close(fd);

    CHECK(pc_psx_rename("bu00:BASLUS-01040VAG1", "bu00:BASLUS-01040VAGA") == 1);
    CHECK(pc_psx_rename("bu00:BASLUS-01040VAG1", "bu00:BASLUS-01040VAG2") == 0);
    fd = pc_psx_open("bu00:BASLUS-01040VAG2", 0x200 | 1 << 16);
    pc_psx_close(fd);

    struct DIRENTRY entries[3];
    CHECK(firstfile("bu00:*", &entries[0]) == &entries[0]);
    CHECK(!strcmp(entries[0].name, "BASLUS-01040VAGA") && entries[0].size == 3 * 8192);
    CHECK(nextfile(&entries[1]) == &entries[1] && !strcmp(entries[1].name, "BASLUS-01040VAG2"));
    CHECK(nextfile(&entries[2]) == NULL);
    CHECK(firstfile("bu00:BASLUS-01040VAG?", &entries[0]) && nextfile(&entries[1]));
    CHECK(firstfile("bu00:OTHER*", &entries[0]) == NULL);

    FILE* f = fopen(path, "rb");
    static unsigned char image[0x20000];
    CHECK(f && fread(image, 1, sizeof image, f) == sizeof image);
    fclose(f);
    CHECK(image[0] == 'M' && image[1] == 'C');
    unsigned char sum = 0;
    for (int i = 0; i < 127; i++)
        sum ^= image[0x80 + i];
    CHECK(sum == image[0xFF]);
    CHECK(image[0x80] == 0x51 && image[0x100] == 0x52 && image[0x180] == 0x53);
    CHECK(!strcmp((char*)image + 0x8A, "BASLUS-01040VAGA"));
    CHECK(!memcmp(image + 0x2000, data, sizeof data));

    CHECK(erase("bu00:BASLUS-01040VAGA") == 1 && erase("bu00:BASLUS-01040VAGA") == 0);
    CHECK(firstfile("bu00:*", &entries[0]) && !strcmp(entries[0].name, "BASLUS-01040VAG2"));
    CHECK(_card_format(0x00) == 1 && firstfile("bu00:*", &entries[0]) == NULL);
    CHECK(_card_format(0x10) == 0);
    unlink(path);
    puts("PASS: memory card files, listing, async events, rename/erase/format and .mcd layout");
    return 0;
}
