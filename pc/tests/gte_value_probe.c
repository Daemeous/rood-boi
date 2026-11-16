/* TEMPORARY: compares the GTE value functions (pc/src/gte_value.h) with the
 * old register interface (psycross inline_c.h macros on the global GTE) on
 * random and edge inputs: every result field and the FLAG word, with the
 * widescreen narrowing off and on, and checks a value call leaves the global
 * registers as they were.
 *
 * Delete this file, its ctest entry and its CMake lines together with the old
 * GTE state machine (pc/docs/plans/2026-11-08-off-the-ps1.md, Picture lane
 * step 1): once nothing calls the register interface there is nothing to
 * compare with. */
#include <libgte.h>
#include "psx/gtereg.h"
#include "psx/inline_c.h"
#include "gte_value.h"

/* (No system headers: PsyCross's types.h and sys/types.h disagree on u_long.) */
extern int dprintf(int fd, const char* format, ...);
extern int puts(const char* text);
extern int memcmp(const void* a, const void* b, __SIZE_TYPE__ size);
extern int pc_gte_widescreen;

/* (vs_gte_apply_matrix_lv calls the SDK routine, which is not part of this probe.) */
VECTOR* ApplyMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1)
{
    (void)m;
    (void)v0;
    return v1;
}

static unsigned long long rng = 0x9e3779b97f4a7c15ull;
static unsigned next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned)(rng >> 16);
}

static const int edge16[] = { 0, 1, -1, 2, 0x7fff, -0x8000, 0x7ffe, -0x7fff, 0x1000, -0x1000, 0x800, 0x4000, -0x4000 };
static const int edge32[] = { 0, 1, -1, 0x7fffffff, (int)0x80000000, 0x7ffff, -0x80000, 0x1000, 0x10000, -0x10000,
    0x3fffffff, -0x40000000 };

static short pick16(int mode)
{
    if (mode == 0)
        return (short)edge16[next() % (sizeof edge16 / sizeof *edge16)];
    if (mode == 1)
        return (short)((int)(next() % 8193) - 4096);
    return (short)next();
}

static int pick32(int mode)
{
    if (mode == 0)
        return edge32[next() % (sizeof edge32 / sizeof *edge32)];
    if (mode == 1)
        return (int)(next() % 0x200001) - 0x100000;
    return (int)next();
}

static int failures;
#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            if (++failures < 20) { \
                dprintf(2, "FAIL %s:%d: ", __FILE__, __LINE__); \
                dprintf(2, __VA_ARGS__); \
                dprintf(2, "\n"); \
            } \
        } \
    } while (0)

static void load_old(const MATRIX* m, const vs_gte_screen* s, const SVECTOR* v)
{
    gte_SetRotMatrix(m);
    gte_SetTransMatrix(m);
    C2_OFX = s->ofx;
    C2_OFY = s->ofy;
    C2_H = s->h;
    C2_DQA = s->dqa;
    C2_DQB = s->dqb;
    gte_ldv0(v);
}

static void compare(const char* what, const vs_gte_result* r, int project, const MATRIX* m, const SVECTOR* v)
{
    int i;
    CHECK(r->flag == CFC2(31), "%s flag %08x vs %08x (v %d %d %d rot %d %d %d t %d %d %d)", what, r->flag,
        CFC2(31), v->vx, v->vy, v->vz, m->m[0][0], m->m[0][1], m->m[0][2], m->t[0], m->t[1], m->t[2]);
    for (i = 0; i < 3; ++i) {
        CHECK(r->ir[i] == (short)(MFC2(9 + i) & 0xffff), "%s ir%d", what, i + 1);
        CHECK((unsigned)r->mac[1 + i] == MFC2(25 + i), "%s mac%d %d vs %d", what, i + 1, r->mac[1 + i],
            (int)MFC2(25 + i));
    }
    if (project) {
        uint sxy, otz;
        gte_stsxy(&sxy);
        gte_stszotz(&otz);
        CHECK((uint)r->sxy == sxy, "%s sxy %08x vs %08x", what, r->sxy, sxy);
        CHECK(r->otz == otz, "%s otz", what);
        CHECK(r->sz == MFC2(19), "%s sz", what);
        CHECK(r->sx == (short)(sxy & 0xffff) && r->sy == (short)(sxy >> 16), "%s sx sy", what);
        CHECK(r->ir0 == (short)MFC2(8), "%s ir0", what);
        CHECK((unsigned)r->mac[0] == MFC2(24), "%s mac0", what);
    }
}

/* The value ops on plain values (nclip, avsz3, op0, dpcs, light transform) and RotAverage4, against the register sequences
 * the game's callers used. */
static void ops_round(int mode)
{
    unsigned a = (unsigned)pick32(mode), b = (unsigned)pick32(mode), c = (unsigned)pick32(mode), d = (unsigned)pick32(mode);
    int zsf = pick16(mode), i;
    unsigned ir[3], mac[3], llm[5], bk[3];
    short sv[3], out[3];
    int vmac[3];
    GTERegisters junk;
    for (i = 0; i < 32; ++i) {
        gte_scratch.CP2D.p[i].d = next();
        gte_scratch.CP2C.p[i].d = next();
    }
    junk = gte_scratch;
    /* nclip: MTC2 12..14, gte_nclip, MFC2(24). */
    MTC2(a, 12), MTC2(b, 13), MTC2(c, 14);
    gte_nclip();
    CHECK((unsigned)vs_gte_nclip(a, b, c) == MFC2(24), "nclip");
    /* avsz3 with the game's ZSF3. */
    CTC2(zsf, 29);
    MTC2(a, 17), MTC2(b, 18), MTC2(c, 19);
    gte_avsz3();
    CHECK(vs_gte_avsz3(zsf, a, b, c) == MFC2(7), "avsz3");
    /* op0: the rotation diagonal by CTC2, IR by MTC2 (the weapon packets' split test). */
    CTC2(a, 0), CTC2(b, 2), CTC2(c, 4);
    MTC2(d, 9), MTC2(a ^ b, 10), MTC2(c ^ d, 11);
    gte_op0();
    for (i = 0; i < 3; ++i)
        mac[i] = MFC2(25 + i);
    vs_gte_op0(a, b, c, d, a ^ b, c ^ d, vmac);
    for (i = 0; i < 3; ++i)
        CHECK((unsigned)vmac[i] == mac[i], "op0 mac%d", i + 1);
    /* dpcs: far colour by CTC2 21..23, RGBC by MTC2 6, IR0 by MTC2 8, RGB2 by MFC2 22. */
    CTC2(a, 21), CTC2(a, 22), CTC2(a, 23);
    MTC2(b, 6), MTC2(c & 0xffff, 8);
    doCOP2(0x0780010);
    CHECK(vs_gte_dpcs(a, b, c & 0xffff) == MFC2(22), "dpcs");
    /* The light matrix and the background colour (MVMVA sf 1, mx 1, v 0, cv 1). */
    for (i = 0; i < 5; ++i)
        llm[i] = next();
    for (i = 0; i < 3; ++i)
        bk[i] = next();
    for (i = 0; i < 3; ++i)
        sv[i] = pick16(mode);
    for (i = 0; i < 5; ++i)
        CTC2(llm[i], 8 + i);
    for (i = 0; i < 3; ++i)
        CTC2(bk[i], 13 + i);
    MTC2((unsigned short)sv[0] | ((unsigned)(unsigned short)sv[1] << 16), 0);
    MTC2((unsigned)(int)sv[2], 1);
    doCOP2(0x04A2012);
    vs_gte_light_transform(llm, bk, sv, out);
    for (i = 0; i < 3; ++i)
        CHECK(out[i] == (short)(MFC2(9 + i) & 0xffff), "light ir%d", i + 1);
    (void)ir;
    (void)junk;
    (void)d;
}

/* RotAverage4's register sequence (CMB_02.s): RTPT, RTPS, AVSZ4. */
static void average4_round(int mode)
{
    MATRIX m;
    SVECTOR v[4];
    vs_gte_screen s = vs_gte_screen_current();
    vs_gte_view view;
    vs_gte_result4 r;
    short in[4][3];
    unsigned sxy[4], flag1, otz;
    int i, j;
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j)
            m.m[i][j] = pick16(mode);
        m.t[i] = pick32(mode);
    }
    s.ofx = pick32(mode), s.ofy = pick32(mode), s.h = pick16(mode), s.dqa = pick16(mode), s.dqb = pick32(mode);
    s.zsf3 = pick16(mode), s.zsf4 = pick16(mode);
    for (i = 0; i < 4; ++i) {
        v[i].vx = in[i][0] = pick16(mode);
        v[i].vy = in[i][1] = pick16(mode);
        v[i].vz = in[i][2] = pick16(mode);
        v[i].pad = 0;
    }
    view = vs_gte_view_of(&m, s);
    r = vs_gte_run4(&view, in, 0);
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    CTC2(s.ofx, 24), CTC2(s.ofy, 25), CTC2(s.h, 26), CTC2(s.dqa, 27), CTC2(s.dqb, 28), CTC2(s.zsf3, 29), CTC2(s.zsf4, 30);
    gte_ldv0(&v[0]);
    gte_ldv1(&v[1]);
    gte_ldv2(&v[2]);
    gte_rtpt();
    sxy[0] = MFC2(12), sxy[1] = MFC2(13), sxy[2] = MFC2(14);
    flag1 = CFC2(31);
    gte_ldv0(&v[3]);
    gte_rtps();
    sxy[3] = MFC2(14);
    doCOP2(0x168002E);
    otz = MFC2(7);
    CHECK(r.sxy[0] == (int)sxy[0] && r.sxy[1] == (int)sxy[1] && r.sxy[2] == (int)sxy[2] && r.sxy[3] == (int)sxy[3], "average4 sxy");
    CHECK(r.otz == otz, "average4 otz %u vs %u", r.otz, otz);
    CHECK(r.ir0 == (short)MFC2(8), "average4 ir0");
    (void)flag1;
}

/* func_800BD2B8's sequence (SetRot, SetTrans of a matrix, four RotTrans of constant corners) and vs_actors_applyLockedBoneTransform's (the
 * parent matrix by eight CTC2 words, the vector by two MTC2 words, RTV0TR, MAC1-3 by MFC2): the same transform on values. */
static void sequences_round(int mode)
{
    MATRIX m;
    SVECTOR v = { 80, -80, 150, 0 };
    vs_gte_view view;
    vs_gte_result r;
    const unsigned* words = (const unsigned*)&m;
    int i, j;
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j)
            m.m[i][j] = pick16(mode);
        m.t[i] = pick32(mode);
    }
    view = vs_gte_view_of(&m, (vs_gte_screen) { 0 });
    r = vs_gte_transform(&view, &v);
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    gte_ldv0(&v);
    gte_rtv0tr();
    for (i = 0; i < 3; ++i)
        CHECK((unsigned)r.mac[1 + i] == MFC2(25 + i), "BD2B8 mac%d", i + 1);
    for (i = 0; i < 8; ++i)
        CTC2(words[i], i);
    v.vx = pick16(mode), v.vy = pick16(mode), v.vz = pick16(mode);
    MTC2(((const unsigned*)&v)[0], 0);
    MTC2(((const unsigned*)&v)[1], 1);
    doCOP2(0x0480012);
    r = vs_gte_transform(&view, &v);
    for (i = 0; i < 3; ++i)
        CHECK((unsigned)r.mac[1 + i] == MFC2(25 + i), "B26B8 mac%d", i + 1);
}

int main(void)
{
    int round, wide;
    for (wide = 0; wide < 2; ++wide) {
        pc_gte_widescreen = wide;
        for (round = 0; round < 60000; ++round) {
            int mode = round % 3;
            MATRIX m;
            SVECTOR v = { 0, 0, 0, 0 };
            vs_gte_screen s;
            vs_gte_view view;
            vs_gte_result r;
            GTERegisters before;
            int i, j;
            for (i = 0; i < 3; ++i) {
                for (j = 0; j < 3; ++j)
                    m.m[i][j] = pick16(mode);
                m.t[i] = pick32(mode);
            }
            v.vx = pick16(mode);
            v.vy = pick16(mode);
            v.vz = pick16(mode);
            v.pad = (short)next();
            s.ofx = pick32(mode);
            s.ofy = pick32(mode);
            s.h = (short)(mode == 2 ? next() : (next() % 3 ? next() % 2000 : pick16(0)));
            s.dqa = pick16(mode);
            s.dqb = pick32(mode);
            /* Junk in the global registers: a value call must not read it. */
            for (i = 0; i < 32; ++i) {
                gte_scratch.CP2D.p[i].d = next();
                gte_scratch.CP2C.p[i].d = next();
            }
            before = gte_scratch;
            view = vs_gte_view_of(&m, s);
            r = vs_gte_project(&view, &v);
            CHECK(memcmp(&before, &gte_scratch, sizeof before) == 0, "project changed the global registers");
            load_old(&m, &s, &v);
            gte_rtps();
            compare("rtps", &r, 1, &m, &v);

            r = vs_gte_transform(&view, &v);
            load_old(&m, &s, &v);
            gte_rtv0tr();
            compare("rtv0tr", &r, 0, &m, &v);

            r = vs_gte_rotate(&view, &v);
            load_old(&m, &s, &v);
            gte_rtv0();
            compare("rtv0", &r, 0, &m, &v);

            /* Zero translation (SetRotMatrix; gte_zrtr), translation from a vector. */
            {
                vs_gte_view rot = vs_gte_view_rotation(&m, s);
                VECTOR t = { pick32(mode), pick32(mode), pick32(mode), 0 };
                vs_gte_view at = vs_gte_view_at(&m, &t, s);
                r = vs_gte_transform(&rot, &v);
                load_old(&m, &s, &v);
                CTC2(0, 5);
                CTC2(0, 6);
                CTC2(0, 7);
                gte_rtv0tr();
                compare("rtv0tr zrtr", &r, 0, &m, &v);
                r = vs_gte_project(&at, &v);
                load_old(&m, &s, &v);
                gte_SetTransVector(&t);
                gte_rtps();
                compare("rtps at", &r, 1, &m, &v);
            }
        }
    }
    {
        int round, mode;
        for (round = 0; round < 40000; ++round) {
            mode = round % 3;
            ops_round(mode);
            average4_round(mode);
            sequences_round(mode);
        }
    }
    {
        /* The projection the game set (through CTC2, as SetGeomOffset and SetGeomScreen do) is read back as set. */
        vs_gte_screen s = { 160 << 16, 112 << 16, 800, -98, 340, 341, 256 };
        vs_gte_screen got;
        pc_gte_widescreen = 0;
        CTC2(s.ofx, 24);
        CTC2(s.ofy, 25);
        CTC2(s.h, 26);
        CTC2(s.dqa, 27);
        CTC2(s.dqb, 28);
        CTC2(s.zsf3, 29);
        CTC2(s.zsf4, 30);
        got = vs_gte_screen_current();
        CHECK(!memcmp(&s, &got, sizeof s), "vs_gte_screen_current");
    }
    if (failures) {
        dprintf(2, "%d failures\n", failures);
        return 1;
    }
    puts("gte_value_probe ok");
    return 0;
}
