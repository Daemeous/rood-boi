/* Host CD drive: the libds command layer, its ready-callback system and the
 * libcd streaming library (St*), over the user's disc image.
 *
 * Normal file loads stay synchronous (pc_disc_read in boot_runtime.c, from
 * main.c's _diskInitRead). This drive serves what must run in time with the
 * game: movie streaming (DsRead2 into the St* ring buffer, TITLE 22C.c) and
 * real-time XA audio. A read command makes the drive deliver sectors from
 * the vsync tick (pc_cd_tick) at the disc's rate, 75 or 150 sectors a second
 * for normal or double speed, as the PS1 drive does:
 *  - in real-time ADPCM mode (DslModeRT), XA audio sectors (matching the
 *    Setfilter file/channel when DslModeSF is set) play through the SPU's CD
 *    input (xa_audio.c) and never reach the CPU;
 *  - other sectors are data: in streaming mode (DsRead2 with DslModeStream)
 *    they go to the St* ring, otherwise the ready callback (or the
 *    DsStartReadySystem handler) gets DslDataReady and can copy the sector
 *    with DsGetSector.
 * Commands take effect at once; their completion callbacks (DslComplete) are
 * delivered at the next tick, as interrupts that follow the command. The
 * streaming library's polling (StGetNext) lets a vertical blank pass while
 * the drive is still filling the ring: on the PS1 the movie loop busy-waits
 * on the CPU while the drive delivers, and the host would otherwise spin
 * through the game's poll budget in an instant. */
#include "psx_types.h" /* psx_long: the SDK's 32-bit long on every build */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*ready_cb)(unsigned char, unsigned char*);
typedef void (*system_cb)(unsigned char, unsigned char*, psx_ulong*);
typedef void (*stream_cb)(void);

extern void pc_log(const char*);
extern void pc_vblank_wait(void);
void pc_xa_play_sector(const unsigned char* sector, const unsigned char atv[4]);
void pc_xa_reset(void);
void pc_spu_clear_cd(void);

enum {
    SECTOR = 2352,
    /* libds commands, mode bits and interrupt kinds (include/psx/libds.h). */
    Nop = 0x01, Setloc = 0x02, Play = 0x03, ReadN = 0x06, Standby = 0x07, Stop = 0x08,
    Pause = 0x09, Init = 0x0A, Mute = 0x0B, Demute = 0x0C, Setfilter = 0x0D, Setmode = 0x0E,
    Getparam = 0x0F, GetlocL = 0x10, GetlocP = 0x11, GetTN = 0x13, GetTD = 0x14, SeekL = 0x15,
    SeekP = 0x16, ReadS = 0x1B,
    ModeStream = 0x100, ModeSpeed = 0x80, ModeRT = 0x40, ModeSize1 = 0x20, ModeSize0 = 0x10,
    ModeSF = 0x08,
    DataReady = 1, Complete = 2, DataEnd = 4, DiskError = 5,
    StatRead = 0x20, StatStandby = 0x02,
    Ready = 1, Busy = 2,
};

static struct {
    FILE* disc;
    psx_long sectors;          /* image size */
    int reading, stream, muted;
    int lba, setloc;       /* next sector; Setloc target */
    int mode;              /* Setmode byte */
    unsigned char filter_file, filter_chan;
    unsigned char atv[4];  /* CD(L)->L, CD(L)->R, CD(R)->R, CD(R)->L */
    unsigned fraction;     /* sector clock: 60000ths of a sector */
    unsigned char sector[SECTOR];
    int sector_lba, sector_read; /* current data sector; DsGetSector offset */
    ready_cb ready;
    struct {
        int active;
        system_cb func;
        ready_cb saved_ready;
    } system;
    /* Command completions waiting for the next tick. */
    struct {
        ready_cb cb;
        unsigned char intr, result[8];
    } done[8];
    int done_count, next_id;
} cd = { .atv = { 0x80, 0, 0x80, 0 }, .next_id = 1, .sector_lba = -1 };

static int bcd(int v) { return v / 10 * 16 + v % 10; }
static int from_bcd(int b) { return b / 16 * 10 + b % 16; }
static int loc_to_lba(const unsigned char* p)
{
    return (from_bcd(p[0]) * 60 + from_bcd(p[1])) * 75 + from_bcd(p[2]) - 150;
}
static void lba_to_loc(int lba, unsigned char* p)
{
    lba += 150;
    p[0] = bcd(lba / 4500);
    p[1] = bcd(lba / 75 % 60);
    p[2] = bcd(lba % 75);
    p[3] = 0;
}

static int open_disc(void)
{
    if (cd.disc)
        return 1;
    cd.disc = fopen("disks/SLUS-01040.bin", "rb");
    if (!cd.disc) {
        perror("[cd] disc image");
        return 0;
    }
    fseek(cd.disc, 0, SEEK_END);
    cd.sectors = ftell(cd.disc) / SECTOR;
    return 1;
}

static unsigned char status(void)
{
    return StatStandby | (cd.reading ? StatRead : 0);
}

static void complete(ready_cb cb, unsigned char intr, const unsigned char* result)
{
    if (!cb)
        return;
    if (cd.done_count == 8) {
        fprintf(stderr, "[cd] completion queue full; callback dropped\n");
        return;
    }
    cd.done[cd.done_count].cb = cb;
    cd.done[cd.done_count].intr = intr;
    memcpy(cd.done[cd.done_count].result, result, 8);
    ++cd.done_count;
}

static void stop_reading(void)
{
    cd.reading = 0;
    pc_spu_clear_cd(); /* the drive's ADPCM output stops with it */
}

/* Executes a drive command; result gets up to 8 response bytes. */
static void execute(unsigned char com, const unsigned char* param, unsigned char* result)
{
    memset(result, 0, 8);
    switch (com) {
    case Setloc:
        if (param)
            cd.setloc = loc_to_lba(param);
        break;
    case ReadN:
    case ReadS:
        if (!open_disc())
            break;
        if (!cd.reading)
            cd.fraction = 0;
        cd.lba = cd.setloc;
        cd.reading = 1;
        pc_xa_reset();
        break;
    case SeekL:
    case SeekP:
        cd.reading = 0;
        cd.lba = cd.setloc;
        break;
    case Pause:
    case Stop:
        stop_reading();
        break;
    case Init:
        stop_reading();
        cd.mode = 0;
        cd.muted = 0;
        break;
    case Mute:
        cd.muted = 1;
        pc_spu_clear_cd();
        break;
    case Demute:
        cd.muted = 0;
        break;
    case Setfilter:
        if (param) {
            cd.filter_file = param[0];
            cd.filter_chan = param[1];
        }
        break;
    case Setmode:
        if (param)
            cd.mode = param[0];
        break;
    case Getparam:
        result[1] = cd.mode;
        result[3] = cd.filter_file;
        result[4] = cd.filter_chan;
        break;
    case GetlocL:
        /* Header and subheader of the last data sector. */
        if (cd.sector_lba >= 0)
            memcpy(result, cd.sector + 12, 8);
        break;
    case GetlocP: {
        int lba = cd.reading ? cd.lba : cd.setloc;
        unsigned char rel[4], abs[4];
        lba_to_loc(lba - 150, rel);
        lba_to_loc(lba, abs);
        result[0] = 1; /* track 1, index 1 */
        result[1] = 1;
        memcpy(result + 2, rel, 3);
        memcpy(result + 5, abs, 3);
        return;
    }
    case GetTN:
        result[1] = 1;
        result[2] = 1;
        break;
    case GetTD:
        if (param && from_bcd(param[0]) == 0 && open_disc()) {
            unsigned char end[4];
            lba_to_loc((int)cd.sectors, end);
            result[1] = end[0];
            result[2] = end[1];
        } else {
            result[2] = 2; /* track 1 at 00:02 */
        }
        break;
    case Play: {
        static int warned;
        if (!warned++)
            pc_log("CD-DA play requested; the disc image has no audio tracks");
        break;
    }
    default:
        break;
    }
    result[0] = status();
}

int DsControl(unsigned char com, unsigned char* param, unsigned char* result)
{
    unsigned char response[8];
    execute(com, param, response);
    if (result)
        memcpy(result, response, 8);
    return 1;
}
/* Blocking form: commands already complete before returning. */
int DsControlB(unsigned char com, unsigned char* param, unsigned char* result)
{
    return DsControl(com, param, result);
}

int DsCommand(unsigned char com, unsigned char* param, ready_cb cbsync, int count)
{
    unsigned char result[8];
    (void)count; /* retries: host commands do not fail */
    execute(com, param, result);
    complete(cbsync, Complete, result);
    int id = cd.next_id++;
    if (cd.next_id <= 0)
        cd.next_id = 1;
    return id;
}

/* Set mode, set location, then the command, as one queued packet. */
int DsPacket(unsigned char mode, unsigned char* pos, unsigned char com, ready_cb func, int count)
{
    unsigned char result[8];
    execute(Setmode, &mode, result);
    if (pos)
        execute(Setloc, pos, result);
    cd.stream = 0;
    return DsCommand(com, 0, func, count);
}

/* Streaming read (libds DSREAD2.c): with DslModeStream the data sectors go to
 * the St* ring buffer; the mode's low byte is the drive mode. */
int DsRead2(unsigned char* pos, int mode)
{
    int id = DsPacket(mode & 0xFF, pos, ReadS, 0, -1);
    cd.stream = (mode & ModeStream) != 0;
    return id;
}

ready_cb DsReadyCallback(ready_cb func)
{
    ready_cb old = cd.ready;
    cd.ready = func;
    return old;
}

/* Hands every data sector to func (with DslDataReady) until
 * DsEndReadySystem, which also pauses the drive. count (retries) is unused. */
int DsStartReadySystem(system_cb func, int count)
{
    (void)count;
    if (cd.system.active)
        return 0;
    cd.system.active = 1;
    cd.system.func = func;
    cd.system.saved_ready = cd.ready;
    return 1;
}

void DsEndReadySystem(void)
{
    if (cd.system.active) {
        cd.ready = cd.system.saved_ready;
        unsigned char result[8];
        execute(Pause, 0, result);
    }
    cd.system.active = 0;
}

int DsSystemStatus(void) { return cd.done_count ? Busy : Ready; }

/* Copies `size` words of the current data sector, continuing where the last
 * call stopped. The data starts after the header (DslModeSize1, 2340-byte
 * sectors) or after the subheader (2048 and 2328-byte modes). */
int DsGetSector(void* madr, int size)
{
    int start = (cd.mode & ModeSize1) ? 12 : cd.sector[15] == 1 ? 16 : 24;
    int limit = (cd.mode & ModeSize1) ? 2340 : (cd.mode & ModeSize0) ? 2328 : 2048;
    int bytes = size * 4;
    if (cd.sector_read + bytes > limit)
        bytes = limit - cd.sector_read;
    if (bytes > 0) {
        memcpy(madr, cd.sector + start + cd.sector_read, bytes);
        cd.sector_read += bytes;
    }
    return 1;
}

unsigned char* DsIntToPos(int i, unsigned char* p)
{
    lba_to_loc(i, p);
    return p;
}

/* Drive commands complete synchronously, so there is no queue to flush. */
void DsFlush(void) { cd.done_count = 0; }

int DsMix(unsigned char* attenuation)
{
    memcpy(cd.atv, attenuation, 4);
    return 1;
}

int DsInit(void)
{
    pc_log("CD drive: disc image streaming and XA audio (pc/src/cd_drive.c)");
    return 1;
}

/* ---- Streaming library (libcd St*) -------------------------------------
 * The ring holds whole frames contiguously (the decoder reads a frame's
 * bitstream in one piece); each sector's 32-byte STR header stays with the
 * frame's header, the 2016 data bytes go to the ring. */
typedef struct {
    uint16_t id, type, secCount, nSectors;
    uint32_t frameCount, frameSize;
    uint16_t width, height;
    uint32_t dummy1, dummy2;
    unsigned char loc[4];
} st_header;

enum { MAX_FRAMES = 32, FRAME_DATA = 2016, FREE = 0, FILLING, COMPLETE, LOCKED };
static struct {
    unsigned char* base;
    unsigned bytes;
    struct {
        int state;
        unsigned offset, size, received;
        st_header header;
    } frames[MAX_FRAMES];
    int head, count;
    unsigned write;      /* end of the newest frame */
    uint32_t start, end; /* StSetStream frame range */
    stream_cb on_frame, on_end;
    uint32_t back_frame;
    int back_lba;
} st;

static void st_clear(void)
{
    st.head = st.count = 0;
    st.write = 0;
    st.back_frame = 0;
}

static void st_pop_free(void)
{
    while (st.count && st.frames[st.head].state == FREE) {
        st.head = (st.head + 1) % MAX_FRAMES;
        --st.count;
    }
    if (!st.count)
        st.write = 0;
}

/* Reserves `size` contiguous bytes after the newest frame, wrapping to the
 * start of the ring; fails when they would overlap a live frame. */
static int st_reserve(unsigned size, unsigned* offset)
{
    if (size > st.bytes)
        return 0;
    if (!st.count) {
        *offset = 0;
        return 1;
    }
    unsigned oldest = st.frames[st.head].offset;
    if (st.write > oldest) {
        if (st.write + size <= st.bytes) {
            *offset = st.write;
            return 1;
        }
        if (size <= oldest) {
            *offset = 0;
            return 1;
        }
        return 0;
    }
    if (st.write + size <= oldest) {
        *offset = st.write;
        return 1;
    }
    return 0;
}

static void st_sector(const unsigned char* raw, int lba)
{
    const unsigned char* data = raw + (raw[15] == 2 ? 24 : 16);
    st_header h;
    memcpy(&h, data, 32);
    if (!st.base || h.id != 0x0160 || h.nSectors == 0 || h.nSectors > 32 || h.secCount >= h.nSectors)
        return;
    if (h.frameCount < st.start || (st.end != 0xFFFFFFFF && h.frameCount > st.end))
        return;
    int newest = (st.head + st.count - 1) % MAX_FRAMES;
    if (!st.count || st.frames[newest].state != FILLING || st.frames[newest].header.frameCount != h.frameCount) {
        /* A new frame: drop an incomplete one, start only at its first sector. */
        if (st.count && st.frames[newest].state == FILLING) {
            st.frames[newest].state = FREE;
            st.write = st.frames[newest].offset;
            if (newest == st.head)
                st_pop_free();
            else
                --st.count;
        }
        unsigned offset, size = h.nSectors * FRAME_DATA;
        if (h.secCount != 0 || st.count == MAX_FRAMES || !st_reserve(size, &offset))
            return; /* ring full: the frame is dropped, as on the PS1 */
        newest = (st.head + st.count) % MAX_FRAMES;
        ++st.count;
        st.frames[newest].state = FILLING;
        st.frames[newest].offset = offset;
        st.frames[newest].size = size;
        st.frames[newest].received = 0;
        st.frames[newest].header = h;
        lba_to_loc(lba, st.frames[newest].header.loc);
        st.write = offset + size;
    }
    unsigned bit = 1u << h.secCount;
    if (st.frames[newest].received & bit)
        return;
    st.frames[newest].received |= bit;
    memcpy(st.base + st.frames[newest].offset + h.secCount * FRAME_DATA, data + 32, FRAME_DATA);
    if (st.frames[newest].received == (h.nSectors == 32 ? 0xFFFFFFFFu : (1u << h.nSectors) - 1)) {
        st.frames[newest].state = COMPLETE;
        st.back_frame = h.frameCount;
        st.back_lba = lba + 1;
        if (st.on_frame)
            st.on_frame();
        if (h.frameCount == st.end && st.on_end)
            st.on_end();
    }
}

void StSetRing(psx_ulong* ring_addr, psx_ulong ring_size)
{
    st.base = (unsigned char*)ring_addr;
    st.bytes = ring_size * 2048;
    st_clear();
}

void StClearRing(void) { st_clear(); }

void StUnSetRing(void)
{
    st.base = 0;
    st_clear();
}

void StSetStream(psx_ulong mode, psx_ulong start_frame, psx_ulong end_frame,
    stream_cb func1, stream_cb func2)
{
    (void)mode; /* 16/24-bit: the decoder's concern (DecDCTin) */
    st.start = start_frame;
    st.end = end_frame;
    st.on_frame = func1;
    st.on_end = func2;
    st_clear();
}

static int st_next(psx_ulong** addr, psx_ulong** header)
{
    for (int i = 0; i < st.count; i++) {
        int index = (st.head + i) % MAX_FRAMES;
        int state = st.frames[index].state;
        if (state == FILLING)
            break;
        if (state == COMPLETE) {
            st.frames[index].state = LOCKED;
            *addr = (unsigned long*)(st.base + st.frames[index].offset);
            *header = (unsigned long*)&st.frames[index].header;
            return 0;
        }
    }
    return 1;
}

/* 0 and the oldest complete frame (data, header), or 1 when none is ready.
 * While the drive is still streaming, a poll waits one vertical blank. */
int StGetNext(psx_ulong** addr, psx_ulong** header)
{
    if (!st_next(addr, header))
        return 0;
    if (!st.base || !cd.reading || !cd.stream)
        return 1;
    pc_vblank_wait();
    return st_next(addr, header);
}

int StFreeRing(psx_ulong* base)
{
    for (int i = 0; i < st.count; i++) {
        int index = (st.head + i) % MAX_FRAMES;
        if (st.frames[index].state != FREE
            && st.base + st.frames[index].offset == (unsigned char*)base) {
            st.frames[index].state = FREE;
            st_pop_free();
            return 0;
        }
    }
    return 1;
}

/* Sectors reach the ring from the drive tick; nothing is deferred. */
void StCdInterrupt(void) { }

/* The newest complete frame's number, and the sector after it in loc (where
 * a stalled stream resumes); 0 when no frame has arrived. */
int StGetBackloc(unsigned char* loc)
{
    if (!st.back_frame)
        return 0;
    lba_to_loc(st.back_lba, loc);
    return (int)st.back_frame;
}

/* ---- Drive clock --------------------------------------------------------- */
static void deliver_sector(void)
{
    if (cd.lba < 0 || cd.lba >= cd.sectors || fseek(cd.disc, (long)cd.lba * SECTOR, SEEK_SET)
        || fread(cd.sector, 1, SECTOR, cd.disc) != SECTOR) {
        unsigned char result[8] = { status() };
        cd.reading = 0;
        if (cd.system.active && cd.system.func)
            cd.system.func(DataEnd, result, 0);
        else if (cd.ready)
            cd.ready(DataEnd, result);
        return;
    }
    int lba = cd.lba++;
    int submode = cd.sector[18], form2_audio = cd.sector[15] == 2 && (submode & 4);
    if (form2_audio && (cd.mode & ModeRT)) {
        if (!cd.muted
            && (!(cd.mode & ModeSF)
                || (cd.sector[16] == cd.filter_file && cd.sector[17] == cd.filter_chan)))
            pc_xa_play_sector(cd.sector, cd.atv);
        return;
    }
    cd.sector_lba = lba;
    cd.sector_read = 0;
    if (cd.stream) {
        st_sector(cd.sector, lba);
        return;
    }
    unsigned char result[8];
    result[0] = status();
    memcpy(result + 1, cd.sector + 12, 7); /* position and subheader */
    if (cd.system.active && cd.system.func)
        cd.system.func(DataReady, result, 0);
    else if (cd.ready)
        cd.ready(DataReady, result);
}

/* Once per vsync (pc_tick): pending completions, then this field's sectors. */
void pc_cd_tick(void)
{
    /* Callbacks may queue further commands: those complete next tick. */
    int pending = cd.done_count;
    struct {
        ready_cb cb;
        unsigned char intr, result[8];
    } due[8];
    memcpy(due, cd.done, sizeof due);
    cd.done_count = 0;
    for (int i = 0; i < pending; i++)
        due[i].cb(due[i].intr, due[i].result);
    if (!cd.reading)
        return;
    /* 59.94 fields a second: 75 * 1001 / 60000 sectors per field at 1x. */
    cd.fraction += ((cd.mode & ModeSpeed) ? 150 : 75) * 1001;
    while (cd.reading && cd.fraction >= 60000) {
        cd.fraction -= 60000;
        deliver_sector();
    }
}
