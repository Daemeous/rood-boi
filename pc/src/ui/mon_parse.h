/* Modern menus, the native Score screen's Bestiary: SMALL/MON.BIN (the
 * encyclopaedia's file, read from the disc into host memory, never the game's
 * heap) as MENU9 reads it (src/MENU/MENU9.PRG/238.c _monBinData_t, state 13,
 * _initEnemyList):
 *   150 records of 44 bytes (78 used): s16 zudId, classId, killFlagsOffset,
 *   killFlagsCount, 8 bytes MENU9 uses as scratch, then name[28] (the game's
 *   text encoding; the viewer shows it from name + 2, past a spacing code)
 *   at record 150: the descriptions, a vsString table (u16 offsets in u16
 *   units from the table, then the strings)
 * unlocked: any kill flag j in [offset, offset + count) set in
 * scoredata.enemyKillFlags (j / 32, bit j % 32); prev / next: the ring over
 * the unlocked entries in index order (one entry: itself). Bounds the
 * original never checks are checked here: a short file, an offset outside
 * the flags (locked), a class outside 0..5 (-1), an unterminated string or a
 * description outside the file (empty). Pure C, no game headers
 * (mon_parse_probe tests it on synthetic files). */
#ifndef PC_UI_MON_PARSE_H
#define PC_UI_MON_PARSE_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_MON_RECORDS = 150, PC_MON_RECORD = 44, PC_MON_ENEMIES = 78, PC_MON_NAME = 28, PC_MON_CLASSES = 6,
       PC_MON_FLAG_WORDS = 26 };

typedef struct {
    int zud, cls;            /* cls: 0..5 (human .. evil), -1 out of range */
    int flags_offset, flags_count;
    int unlocked, prev, next; /* prev / next: -1 when locked (or none unlocked) */
    int name_at, name_len;   /* the name's bytes in the file (up to its 0xE7, at most 28) */
    int desc_at, desc_len;   /* the description's bytes (0 / 0: none) */
} pc_mon_entry;

typedef struct {
    int ok;                  /* 1: parsed; 0: see why */
    const char* why;         /* a static reason when !ok */
    int unlocked;            /* entries unlocked */
    pc_mon_entry e[PC_MON_ENEMIES];
} pc_mon_file;

/* Parse `size` bytes of MON.BIN with the kill flags (26 words). */
void pc_mon_parse(const unsigned char* data, unsigned size, const int kill_flags[PC_MON_FLAG_WORDS], pc_mon_file* out);
/* _initEnemyList's rule alone (the probe's reference shares nothing with it):
 * unlocked from the flags. */
int pc_mon_unlocked(int offset, int count, const int kill_flags[PC_MON_FLAG_WORDS]);

#ifdef __cplusplus
}
#endif
#endif
