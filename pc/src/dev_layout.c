/* The layout table in the game (dev_layout.h): loads <executable>.layout, written by pc/tools/layout_table.py. */
#include "dev_layout.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct {
    char kind;
    unsigned size;
    const char* name;
    int target;      /* pointer target, array element */
    unsigned count;  /* array element count */
    int first_field, field_count;
} Type;
typedef struct {
    const char* name;  /* NULL: anonymous */
    unsigned offset;
    int type, bitsize, bitoffset;
} Field;
typedef struct {
    const char* name;
    uintptr_t address;
    unsigned size;
    int type;
} Sym;
typedef struct {
    const char* from;
    const char* to;
} Alias;
typedef struct {
    const char* type_name;
    const char* from;
    const char* to;
} FieldAlias;

static Type* types;
static int type_count;
static Field* fields;
static int field_count;
static Sym* syms;
static int sym_count;
static Alias* aliases;
static int alias_count;
static FieldAlias* field_aliases;
static int field_alias_count;
static int loaded;
static char load_error[320] = "";

static int by_name(const void* a, const void* b) { return strcmp(((const Sym*)a)->name, ((const Sym*)b)->name); }
static int by_alias(const void* a, const void* b) { return strcmp(((const Alias*)a)->from, ((const Alias*)b)->from); }

static void* grow(void* array, int count, size_t size, int* capacity)
{
    if (count < *capacity)
        return array;
    *capacity = *capacity ? *capacity * 2 : 1024;
    return realloc(array, (size_t)*capacity * size);
}

static int split(char* line, char** cols, int max)
{
    int n = 0;
    cols[n++] = line;
    for (char* p = line; *p && n < max; p++)
        if (*p == '\t') {
            *p = 0;
            cols[n++] = p + 1;
        }
    return n;
}

static int executable_path(char* out, size_t size)
{
    const char* override = getenv("ROOD_LAYOUT");
    if (override && *override) {
        snprintf(out, size, "%s", override);
        return 1;
    }
#ifdef _WIN32
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exe, sizeof exe))
        return 0;
    snprintf(out, size, "%s.layout", exe);
#else
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        return 0;
    exe[n] = 0;
    snprintf(out, size, "%s.layout", exe);
#endif
    return 1;
}

static long file_size_of_executable(void)
{
    struct stat st;
#ifdef _WIN32
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exe, sizeof exe) || stat(exe, &st))
        return -1;
#else
    if (stat("/proc/self/exe", &st))
        return -1;
#endif
    return (long)st.st_size;
}

int dev_layout_load(const char** error)
{
    if (loaded > 0) {
        if (error)
            *error = NULL;
        return 1;
    }
    loaded = -1;  /* (a table that is missing or for another build is looked for again on the next call) */
    type_count = field_count = sym_count = alias_count = field_alias_count = 0;
    char path[4200];
    if (!executable_path(path, sizeof path)) {
        snprintf(load_error, sizeof load_error, "no executable path");
        goto done;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        snprintf(load_error, sizeof load_error, "no layout table %s (the build writes it beside the executable)", path);
        goto done;
    }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = malloc((size_t)length + 1);
    if (!text || fread(text, 1, (size_t)length, f) != (size_t)length) {
        fclose(f);
        snprintf(load_error, sizeof load_error, "cannot read %s", path);
        goto done;
    }
    fclose(f);
    text[length] = 0;
    int type_cap = 0, field_cap = 0, sym_cap = 0, alias_cap = 0, falias_cap = 0;
    type_count = 1;  /* id 0 is "no type" */
    for (char* line = text; line && *line;) {
        char* next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char* c[8];
        int n = split(line, c, 8);
        if (!strcmp(c[0], "build") && n >= 2) {
            long want = atol(c[1]);
            if (want != file_size_of_executable()) {
                snprintf(load_error, sizeof load_error,
                    "%s is for another build (it was made for a binary of %ld bytes); relink or run layout_table.py",
                    path, want);
                free(text);
                goto done;
            }
        } else if (!strcmp(c[0], "type") && n >= 6) {
            int id = atoi(c[1]);
            while (type_count <= id) {
                types = grow(types, type_count, sizeof *types, &type_cap);
                memset(&types[type_count++], 0, sizeof *types);
            }
            Type* t = &types[id];
            t->kind = c[2][0];
            t->size = (unsigned)atol(c[3]);
            t->name = strcmp(c[4], "-") ? c[4] : NULL;
            if (t->kind == 'a')
                sscanf(c[5], "%d %u", &t->target, &t->count);
            else
                t->target = strcmp(c[5], "-") ? atoi(c[5]) : 0;
        } else if (!strcmp(c[0], "fld") && n >= 7) {
            int id = atoi(c[1]);
            fields = grow(fields, field_count, sizeof *fields, &field_cap);
            Field* fl = &fields[field_count];
            fl->name = strcmp(c[2], "-") ? c[2] : NULL;
            fl->offset = (unsigned)atol(c[3]);
            fl->type = atoi(c[4]);
            fl->bitsize = atoi(c[5]);
            fl->bitoffset = atoi(c[6]);
            if (id < type_count) {
                if (!types[id].field_count)
                    types[id].first_field = field_count;
                types[id].field_count++;
            }
            field_count++;
        } else if (!strcmp(c[0], "sym") && n >= 6) {
            syms = grow(syms, sym_count, sizeof *syms, &sym_cap);
            Sym* s = &syms[sym_count++];
            s->name = c[1];
            s->address = (uintptr_t)strtoull(c[2], NULL, 16);
            s->size = (unsigned)atol(c[3]);
            s->type = strcmp(c[4], "-") ? atoi(c[4]) : 0;
        } else if (!strcmp(c[0], "alias") && n >= 3) {
            aliases = grow(aliases, alias_count, sizeof *aliases, &alias_cap);
            aliases[alias_count].from = c[1];
            aliases[alias_count++].to = c[2];
        } else if (!strcmp(c[0], "falias") && n >= 4) {
            field_aliases = grow(field_aliases, field_alias_count, sizeof *field_aliases, &falias_cap);
            field_aliases[field_alias_count].type_name = c[1];
            field_aliases[field_alias_count].from = c[2];
            field_aliases[field_alias_count++].to = c[3];
        }
        line = next;
    }
    qsort(syms, (size_t)sym_count, sizeof *syms, by_name);
    qsort(aliases, (size_t)alias_count, sizeof *aliases, by_alias);
    loaded = 1;
    load_error[0] = 0;
done:
    if (error)
        *error = load_error[0] ? load_error : NULL;
    return loaded > 0;
}

static const Sym* find_symbol(const char* name)
{
    Sym key = {.name = name};
    const Sym* s = bsearch(&key, syms, (size_t)sym_count, sizeof *syms, by_name);
    if (s)
        return s;
    Alias akey = {.from = name};
    const Alias* a = bsearch(&akey, aliases, (size_t)alias_count, sizeof *aliases, by_alias);
    if (a) {
        key.name = a->to;
        return bsearch(&key, syms, (size_t)sym_count, sizeof *syms, by_name);
    }
    return NULL;
}

static const Type* type_at(int id) { return id > 0 && id < type_count ? &types[id] : NULL; }

char dev_layout_kind(int id)
{
    const Type* t = type_at(id);
    return t ? t->kind : 0;
}
const char* dev_layout_type_name(int id)
{
    const Type* t = type_at(id);
    return t ? t->name : NULL;
}
unsigned dev_layout_type_size(int id)
{
    const Type* t = type_at(id);
    return t ? t->size : 0;
}
int dev_layout_type_target(int id, unsigned* count)
{
    const Type* t = type_at(id);
    if (count)
        *count = t ? t->count : 0;
    return t ? t->target : 0;
}
int dev_layout_member(int id, int index, const char** name, unsigned* offset, int* member_type, int* bitsize,
    int* bitoffset)
{
    const Type* t = type_at(id);
    if (!t || index < 0 || index >= t->field_count)
        return 0;
    const Field* f = &fields[t->first_field + index];
    *name = f->name;
    *offset = f->offset;
    *member_type = f->type;
    *bitsize = f->bitsize;
    *bitoffset = f->bitoffset;
    return 1;
}

/* member `name` of struct/union `id`: offset, type, bit size and bit offset (from the struct start); 1 if found */
static int find_member(int id, const char* name, unsigned* offset, int* type, int* bitsize, int* bitoffset)
{
    const Type* t = type_at(id);
    if (!t)
        return 0;
    const char* other = NULL;
    for (int i = 0; i < field_alias_count && t->name; i++)
        if (!strcmp(field_aliases[i].type_name, t->name) && !strcmp(field_aliases[i].from, name))
            other = field_aliases[i].to;
    for (int i = 0; i < t->field_count; i++) {
        const Field* f = &fields[t->first_field + i];
        if (f->name && (!strcmp(f->name, name) || (other && !strcmp(f->name, other)))) {
            *offset = f->offset;
            *type = f->type;
            *bitsize = f->bitsize;
            *bitoffset = f->bitoffset;
            return 1;
        }
    }
    for (int i = 0; i < t->field_count; i++) {
        const Field* f = &fields[t->first_field + i];
        const Type* ft = type_at(f->type);
        if (!f->name && ft && (ft->kind == 's' || ft->kind == 'u')) {
            unsigned o;
            int ty, bs, bo;
            if (find_member(f->type, name, &o, &ty, &bs, &bo)) {
                *offset = f->offset + o;
                *type = ty;
                *bitsize = bs;
                *bitoffset = bs ? bo + (int)f->offset * 8 : 0;
                return 1;
            }
        }
    }
    return 0;
}

static int fail(char* error, size_t size, const char* format, const char* a)
{
    snprintf(error, size, format, a ? a : "");
    return 0;
}

static int follow(uintptr_t* address, const Type* t, int (*readable)(uintptr_t, uintptr_t), char* error,
    size_t error_size)
{
    size_t width = sizeof(void*);
    if (!t->target)
        return fail(error, error_size, "a void pointer cannot be followed%s", NULL);
    if (!readable || !readable(*address, width))
        return fail(error, error_size, "pointer not readable%s", NULL);
    uint64_t value = 0;
    memcpy(&value, (void*)*address, width);
    if (!value)
        return fail(error, error_size, "null pointer%s", NULL);
    *address = (uintptr_t)value;
    return 1;
}

static int is_ident_start(int c) { return isalpha(c) || c == '_'; }

int dev_layout_resolve(const char* path, int (*readable)(uintptr_t, uintptr_t), dev_layout_ref* out, char* error,
    size_t error_size)
{
    const char* e;
    if (!dev_layout_load(&e))
        return fail(error, error_size, "%s", e ? e : "no layout table");
    while (isspace((unsigned char)*path))
        path++;
    if (!is_ident_start((unsigned char)*path))
        return fail(error, error_size, "no symbol in the path%s", NULL);
    char name[160];
    size_t n = 0;
    while ((isalnum((unsigned char)path[n]) || path[n] == '_' || path[n] == '@' || path[n] == ':') && n < sizeof name - 1) {
        name[n] = path[n];
        n++;
    }
    name[n] = 0;
    path += n;
    const Sym* sym = find_symbol(name);
    if (!sym)
        return fail(error, error_size, "no symbol %s", name);
    uintptr_t address = sym->address;
    int type = sym->type, bitsize = 0, bitshift = 0;
    unsigned size = sym->size;
    if (!type) {
        if (*path)
            return fail(error, error_size, "%s has no type information", name);
        *out = (dev_layout_ref){address, size, 0, 0, 0, size ? size : (size_t)~0};
        return 1;
    }
    if (type_at(type)->size)
        size = type_at(type)->size;
    size_t limit = size > sym->size ? size : sym->size;
    if (!limit)
        limit = (size_t)~0;  /* an array declared without a bound: its extent is not known */
    while (*path) {
        if (isspace((unsigned char)*path)) {
            path++;
            continue;
        }
        const Type* t = type_at(type);
        if (*path == '[') {
            char* close;
            long index = strtol(path + 1, &close, 0);
            if (*close != ']')
                return fail(error, error_size, "unclosed [%s", NULL);
            path = close + 1;
            if (t->kind == 'a') {
                if (t->count && (index < 0 || (unsigned long)index >= t->count))
                    return fail(error, error_size, "index outside the array%s", NULL);
                type = t->target;
                address += (uintptr_t)index * type_at(type)->size;
                if (limit != (size_t)~0)
                    limit -= (size_t)index * type_at(type)->size;
            } else if (t->kind == 'p') {
                if (!follow(&address, t, readable, error, error_size))
                    return 0;
                type = t->target;
                address += (uintptr_t)index * type_at(type)->size;
                limit = (size_t)~0;
            } else
                return fail(error, error_size, "[] on something that is not an array or pointer%s", NULL);
            size = type_at(type)->size;
            bitsize = bitshift = 0;
            continue;
        }
        int arrow = path[0] == '-' && path[1] == '>';
        if (arrow)
            path += 2;
        else if (*path == '.')
            path++;
        else
            return fail(error, error_size, "unexpected %s in the path", path);
        if (!is_ident_start((unsigned char)*path))
            return fail(error, error_size, "member name expected at %s", path);
        char member[96];
        n = 0;
        while ((isalnum((unsigned char)path[n]) || path[n] == '_') && n < sizeof member - 1) {
            member[n] = path[n];
            n++;
        }
        member[n] = 0;
        path += n;
        if (t->kind == 'p') {
            if (!follow(&address, t, readable, error, error_size))
                return 0;
            type = t->target;
            t = type_at(type);
            limit = (size_t)~0;
        } else if (arrow)
            return fail(error, error_size, "-> on something that is not a pointer%s", NULL);
        if (!t || (t->kind != 's' && t->kind != 'u'))
            return fail(error, error_size, "%s is not a member of a struct", member);
        unsigned offset;
        int member_type, bs, bo;
        if (!find_member(type, member, &offset, &member_type, &bs, &bo))
            return fail(error, error_size, "no member %s", member);
        address += offset;
        if (limit != (size_t)~0)
            limit -= offset;
        type = member_type;
        bitsize = bs;
        bitshift = bs ? bo - (int)offset * 8 : 0;
        size = type_at(type)->size;
    }
    *out = (dev_layout_ref){address, size, type, bitsize, bitshift, limit};
    return 1;
}

static uint64_t load_scalar(const dev_layout_ref* ref, unsigned width)
{
    uint64_t v = 0;
    memcpy(&v, (void*)ref->address, width);
    return v;
}

int dev_layout_format_value(const dev_layout_ref* ref, char* out, size_t size)
{
    const Type* t = type_at(ref->type);
    if (!t)
        return 0;
    if (ref->bitsize) {
        unsigned bytes = (unsigned)(ref->bitshift + ref->bitsize + 7) / 8;
        if (bytes > 8)
            return 0;
        uint64_t v = load_scalar(ref, bytes) >> ref->bitshift;
        v &= ref->bitsize >= 64 ? ~0ull : ((1ull << ref->bitsize) - 1);
        if (t->kind == 'i' && (v >> (ref->bitsize - 1)) & 1)
            v |= ~0ull << ref->bitsize;
        if (t->kind == 'i')
            snprintf(out, size, "%lld", (long long)v);
        else
            snprintf(out, size, "%llu", (unsigned long long)v);
        return 1;
    }
    int word = (t->kind == 's' || t->kind == 'u') && (t->size == 1 || t->size == 2 || t->size == 4 || t->size == 8);
    if (!word && t->kind != 'i' && t->kind != 'n' && t->kind != 'b' && t->kind != 'p' && t->kind != 'f')
        return 0;  /* (a struct or union of one word: a group of bit fields, as a number) */
    unsigned w = t->size;
    if (w == 0 || w > 8)
        return 0;
    if (t->kind == 'f') {
        if (w == 4) {
            float f;
            memcpy(&f, (void*)ref->address, 4);
            snprintf(out, size, "%.9g", (double)f);
        } else if (w == 8) {
            double d;
            memcpy(&d, (void*)ref->address, 8);
            snprintf(out, size, "%.17g", d);
        } else
            return 0;
        return 1;
    }
    uint64_t v = load_scalar(ref, w);
    if (t->kind == 'i') {
        if (w < 8 && (v >> (w * 8 - 1)) & 1)
            v |= ~0ull << (w * 8);
        snprintf(out, size, "%lld", (long long)v);
    } else
        snprintf(out, size, "%llu", (unsigned long long)v);
    return 1;
}

int dev_layout_store_value(const dev_layout_ref* ref, long long value)
{
    const Type* t = type_at(ref->type);
    int word = t && (t->kind == 's' || t->kind == 'u') && (t->size == 1 || t->size == 2 || t->size == 4 || t->size == 8);
    if (!t || (!word && t->kind != 'i' && t->kind != 'n' && t->kind != 'b' && t->kind != 'p'))
        return 0;
    if (ref->bitsize) {
        unsigned bytes = (unsigned)(ref->bitshift + ref->bitsize + 7) / 8;
        if (bytes > 8)
            return 0;
        uint64_t mask = (ref->bitsize >= 64 ? ~0ull : ((1ull << ref->bitsize) - 1)) << ref->bitshift;
        uint64_t v = load_scalar(ref, bytes);
        v = (v & ~mask) | (((uint64_t)value << ref->bitshift) & mask);
        memcpy((void*)ref->address, &v, bytes);
        return 1;
    }
    unsigned w = t->size;
    if (w == 0 || w > 8)
        return 0;
    uint64_t v = (uint64_t)value;
    memcpy((void*)ref->address, &v, w);
    return 1;
}

void dev_layout_each_symbol(const char* prefix, void (*each)(void*, const char*, uintptr_t, unsigned, int),
    void* context)
{
    if (!dev_layout_load(NULL))
        return;
    size_t n = strlen(prefix);
    for (int i = 0; i < sym_count; i++)
        if (!strncmp(syms[i].name, prefix, n))
            each(context, syms[i].name, syms[i].address, syms[i].size, syms[i].type);
}
