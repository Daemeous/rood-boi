/* Explicit native versions of the original room-effect callback tables.
 * Included after 2842C.c definitions so wrappers use the actual function types.
 * The table words are checked against the private disc data by build_boot.py. */
static int pc_cb_80091C6C(D_800F1DD8_t2* p) { return func_80091C6C((func_80091C6C_t*)p); }
static int pc_cb_80091E10(D_800F1DD8_t2* p) { return func_80091E10((func_80091E10_t*)p); }
static int pc_cb_80091FE8(D_800F1DD8_t2* p) { return func_80091FE8((func_80091FE8_t*)p); }
static int pc_cb_800923F8(D_800F1DD8_t2* p) { return func_800923F8((D_800F1DD8_t2*)p); }
static int pc_cb_80092540(D_800F1DD8_t2* p)
{
    (void)p;
    return func_80092540();
}
static int pc_cb_80092C68(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_80092C68((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_80092E14(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_80092E14((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_80092F6C(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_80092F6C((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_80092F74(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_80092F74((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_80092F98(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_80092F98((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_80092FBC(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_80092FBC((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_8009352C(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_8009352C((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_800935B8(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_800935B8((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_80092E7C(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_80092E7C((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_800935F8(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_800935F8((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_8009361C(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_8009361C((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_800936F0(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_800936F0((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
static int pc_cb_80093640(func_80092B04_t* a, func_80092B04_t2* b)
{
    return func_80093640((func_80092F74_t*)a, (func_80092F74_t2*)b);
}
int (*D_800E85CC[])(D_800F1DD8_t2*) = {
    pc_cb_80091C6C,
    pc_cb_80091FE8,
    pc_cb_80091E10,
    pc_cb_80092540,
    pc_cb_80092540,
    pc_cb_80092540,
    pc_cb_800923F8,
};
int (*D_800E85E8[])(func_80092B04_t*, func_80092B04_t2*) = {
    pc_cb_80092C68,
    pc_cb_80092E14,
    pc_cb_80092F6C,
    pc_cb_80092F74,
    pc_cb_80092F98,
    pc_cb_80092FBC,
    pc_cb_8009352C,
    pc_cb_800935B8,
    pc_cb_80092E7C,
    pc_cb_800935F8,
    pc_cb_8009361C,
    pc_cb_800936F0,
    pc_cb_80093640,
};
