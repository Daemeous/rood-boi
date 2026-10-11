/* Guest-pointer and layout helpers for the 64-bit (LP64) port.
 *
 * Why: under LP64 a pointer is 8 bytes, so every struct that overlays PS1
 * data (the original image, disc files, the scratchpad, save data) would
 * shift every field after a pointer. Those structs keep the PS1 layout and
 * store a 4-byte guest address instead: guest memory stays mapped low
 * (RAM at 0x80000000, scratchpad at 0x1F800000), so a zero-extended guest
 * address is a valid host pointer and the accessors cost nothing. Structs
 * the game builds itself (runtime state) take the native layout instead,
 * with their literal sizes and offsets replaced by PSX_SIZE/PSX_OFFSET.
 *
 * The header is safe to include from matched src/ code: on the PS1 (and the
 * 32-bit PC build) psx_ptr(T) is plain T*, the accessors are the identity
 * and PSX_SIZE/PSX_OFFSET expand to the original literal, so objects stay
 * byte-identical. Only an LP64 ROOD_PC build sees the 4-byte type.
 *
 *   typedef struct {
 *       psx_ptr(struct Node) next;    // 4 bytes on every target
 *       short x, y;
 *   } Node;                           // PS1 layout: 8 bytes
 *   PSX_ASSERT_SIZE(Node, 8);
 *   PSX_ASSERT_OFFSET(Node, x, 4);
 *
 *   Node* n = psx_get(node->next);    // read: node->next on the PS1
 *   psx_set(node->next, other);       // write: node->next = other
 *   psx_postinc(p->cursor);           // p->cursor++ (psx_add: +=)
 *   psx_set_raw(p->next, Node*, addr); // p->next = (Node*)addr
 *   u_int a = psx_to_int(u_int, n);    // (u_int)n, n a guest address
 *   n = psx_to_ptr(Node*, a);          // (Node*)a, zero-extended
 *   n = psx_load(Node*, buf + 8);      // a guest pointer stored at buf + 8
 *   psx_store(buf + 8, n);             // store one there
 *   vs_main_bzero(state, PSX_SIZE(State, 0xC4));        // runtime struct
 *   p = (u8*)actor + PSX_OFFSET(Actor, position, 0x34); // byte offsets
 *
 * psx_ptr fields of separately declared structs are distinct C types: copy
 * one into another with psx_copy(dst, src) rather than plain assignment.
 * Declare one field per psx_ptr(T) (on the PS1 "psx_ptr(T) a, b;" would make
 * b a T). Use PSX_ASSERT_* at file scope only.
 *
 * The LP64 branch is chosen by ROOD_PC_LP64, which build_boot.py --arch 64
 * passes, not by __LP64__: src/ is preprocessed without -m32, so host
 * macros do not describe the target. The header includes nothing, because
 * src/ is preprocessed with -nostdinc against the PS1 SDK headers.
 */
#ifndef ROOD_PSX_TYPES_H
#define ROOD_PSX_TYPES_H

#if defined(ROOD_PC_LP64) /* only the PC passes it */

/* A 4-byte, 4-aligned guest address that remembers its target type T. The
 * zero-length member costs no storage; only its type is used. */
#define psx_ptr(T)                                                             \
    struct __attribute__((packed, aligned(4))) {                               \
        __UINT32_TYPE__ raw;                                                   \
        T* psx_type_[0];                                                       \
    }

/* Called when native code stores a host pointer that has no guest address
 * (above 4 GB); provided by the runtime. */
extern void pc_psx_bad_pointer(const void* pointer)
    __attribute__((noreturn));

static inline __UINT32_TYPE__ psx_addr(const void* pointer) {
    __UINTPTR_TYPE__ value = (__UINTPTR_TYPE__)pointer;
    if (value >> 32) {
        pc_psx_bad_pointer(pointer);
    }
    return (__UINT32_TYPE__)value;
}

#define psx_get(field)                                                         \
    ((__typeof__((field).psx_type_[0]))(__UINTPTR_TYPE__)(field).raw)
/* The unevaluated assignment to a T* keeps the type check (and warnings) a
 * plain assignment has; a conditional would also reject char* -> u_char*. */
#define psx_set(field, pointer)                                                \
    ((void)sizeof((__typeof__((field).psx_type_[0])){0} = (pointer)),          \
     (field).raw = psx_addr(pointer))
#define psx_copy(dst, src) ((dst).raw = (src).raw)
#define psx_raw(field) ((field).raw)
/* field++ and field += n: pointer arithmetic in units of T, same results;
 * field is evaluated once. */
#define psx_postinc(field)                                                     \
    ({                                                                         \
        __typeof__(&(field)) psx_f_ = &(field);                                \
        __typeof__(psx_get(*psx_f_)) psx_old_ = psx_get(*psx_f_);              \
        psx_set(*psx_f_, psx_old_ + 1);                                        \
        psx_old_;                                                              \
    })
#define psx_add(field, n)                                                      \
    ({                                                                         \
        __typeof__(&(field)) psx_f_ = &(field);                                \
        __typeof__(psx_get(*psx_f_)) psx_new_ = psx_get(*psx_f_) + (n);        \
        psx_set(*psx_f_, psx_new_);                                            \
        psx_new_;                                                              \
    })
/* field = (T)value for a guest address held as an integer (or a pointer
 * already known to be a guest address): stored without the range check.
 * T must be the field's pointer type. */
#define psx_set_raw(field, T, value)                                           \
    ((void)sizeof(0 ? (field).psx_type_[0] : (T)0),                            \
     (T)(__UINTPTR_TYPE__)((field).raw                                         \
                           = (__UINT32_TYPE__)(__UINTPTR_TYPE__)(value)))

/* (T)integer and (T)pointer for guest addresses kept in 32-bit integers
 * (sound command parameters, offsets added to a pointer's address): the
 * integer is zero-extended (a sign-extended 0x80000000 would be a bad
 * pointer) and the pointer is checked to have a guest address. */
#define psx_to_ptr(T, value)                                                   \
    ((T)(__UINTPTR_TYPE__)(__UINT32_TYPE__)(value))
#define psx_to_int(T, pointer) ((T)psx_addr(pointer))
/* A pointer or an integer as a 32-bit word, for SDK macros that take either
 * (setaddr: a primitive or a link word). Pointers are checked like
 * psx_to_int; integers are truncated as the PS1's u_long cast does. */
#define psx_word(value)                                                        \
    __builtin_choose_expr(                                                     \
        __builtin_classify_type(value) == 5 /* pointer_type_class */,          \
        psx_addr((const void*)(__UINTPTR_TYPE__)(value)),                      \
        (__UINT32_TYPE__)(__UINTPTR_TYPE__)(value))

#else /* PS1, or the 32-bit PC build: pointers are already 4 bytes. */

#define psx_ptr(T) T*
#define psx_get(field) (field)
#define psx_set(field, pointer) ((field) = (pointer))
#define psx_copy(dst, src) ((dst) = (src))
#define psx_raw(field) ((unsigned long)(field))
#define psx_postinc(field) ((field)++)
#define psx_add(field, n) ((field) += (n))
#define psx_set_raw(field, T, value) ((field) = (T)(value))
#define psx_to_ptr(T, value) ((T)(value))
#define psx_to_int(T, pointer) ((T)(pointer))
#define psx_word(value) ((unsigned long)(value))

#endif

/* A guest pointer stored at a byte address, for port code that reads PS1
 * layouts by offset (the translated BATTLE assembly: `*(T*)(base + 0x68)`,
 * scratchpad slots): the word is 4 bytes on every target, so load it as a
 * u32 and widen it, never as a host pointer.
 *   cb = psx_load(int (*)(void*), base + 0x68);  // *(int (**)(void*))(base + 0x68)
 *   psx_store(base + 0x18c, record);             // *(void**)(base + 0x18c) = record
 * On ILP32 both are exactly the pointer-typed load and store. */
#define psx_load(T, address)                                                   \
    psx_to_ptr(T, *(const __UINT32_TYPE__*)(const void*)(address))
#define psx_store(address, pointer)                                            \
    ((void)(*(__UINT32_TYPE__*)(void*)(address)                                \
             = psx_to_int(__UINT32_TYPE__, pointer)))

/* The k-th 4-byte guest pointer word from base, for game code that reads and
 * writes PS1 pointer slots through T** (the scratchpad's packet cursor and OT
 * base: `*(void**)0x1F800000`, `((u_long**)getScratchAddr(0))[1]`). On the
 * PS1 and the 32-bit build they are exactly the original `((T*)(base))[k]`
 * (so matched code compiles the same); under LP64 the stride stays 4 bytes.
 *   poly = psx_slot_get(POLY_G4*, getScratchAddr(0), 0); // *(POLY_G4**)...
 *   psx_slot_set(void*, scratch, 0, poly + 1);            // scratch[0] = ...
 * base may be an integer address or any pointer. */
#if defined(ROOD_PC_LP64) /* only the PC passes it */
#define psx_slot_get(T, base, k)                                               \
    psx_load(T, (__UINTPTR_TYPE__)(base) + 4 * (k))
#define psx_slot_set(T, base, k, value)                                        \
    psx_store((__UINTPTR_TYPE__)(base) + 4 * (k), (T)(value))
#else
#define psx_slot_get(T, base, k) (((T*)(base))[k])
#define psx_slot_set(T, base, k, value) (((T*)(base))[k] = (value))
#endif

/* The SDK's long: 32 bits on the PS1. The 64-bit build reads the SDK headers
 * with long spelled int (pc/include/psx_sdk_long_begin.h), so port code that
 * defines or calls an SDK function spells the type psx_long to match the
 * prototype on every build. */
#if defined(ROOD_PC_LP64) /* only the PC passes it */
typedef int psx_long;
typedef unsigned int psx_ulong;
#else
typedef long psx_long;
typedef unsigned long psx_ulong;
#endif

#ifdef ROOD_PC

#ifdef __cplusplus
#define PSX_STATIC_ASSERT_ static_assert
#else
#define PSX_STATIC_ASSERT_ _Static_assert
#endif

/* Load-bearing PS1 layouts (data overlays): fail the build, not memory. */
#define PSX_ASSERT_SIZE(T, n)                                                  \
    PSX_STATIC_ASSERT_(sizeof(T) == (n), "PS1 layout: sizeof(" #T ") != " #n)
#define PSX_ASSERT_OFFSET(T, field, n)                                         \
    PSX_STATIC_ASSERT_(__builtin_offsetof(T, field) == (n),                    \
                       "PS1 layout: offsetof(" #T ", " #field ") != " #n)

/* A size or offset the original code wrote as a literal. Native layouts use
 * the real value; wherever pointers are 4 bytes (the 32-bit build) the
 * literal is also checked, so a wrong T or field fails to compile. The
 * result is an int, like the literal, so signed arithmetic is unchanged. */
#define PSX_CHECK_ILP32_(actual, n)                                            \
    (0 * sizeof(char[(sizeof(void*) != 4 || (actual) == (n)) ? 1 : -1]))
#define PSX_SIZE(T, n) ((int)(sizeof(T) + PSX_CHECK_ILP32_(sizeof(T), n)))
#define PSX_OFFSET(T, field, n)                                                \
    ((int)(__builtin_offsetof(T, field)                                        \
           + PSX_CHECK_ILP32_(__builtin_offsetof(T, field), n)))

#else /* PS1: the original literal, so matched objects do not change. */

/* A declaration, so the trailing semicolon stays valid at file scope. */
#define PSX_ASSERT_SIZE(T, n) extern int psx_assert_unused_
#define PSX_ASSERT_OFFSET(T, field, n) extern int psx_assert_unused_
#define PSX_SIZE(T, n) (n)
#define PSX_OFFSET(T, field, n) (n)

#endif

#endif
