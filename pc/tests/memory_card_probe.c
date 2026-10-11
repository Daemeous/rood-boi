/* File-backed memory card: BIOS file calls, directory listing, events and the
 * raw .mcd image they persist. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main(void)
{
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
