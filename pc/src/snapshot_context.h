/* The game side's registers at the vsync boundary (snapshot.c), shared with
 * the in-process rewind (state_rewind.c): setjmp/longjmp without glibc's
 * pointer mangling (a mangled jmp_buf only works in the process that made it).
 * Part of the save state file's header: its layout is the file format's. */
#ifndef ROOD_SNAPSHOT_CONTEXT_H
#define ROOD_SNAPSHOT_CONTEXT_H
#include <stdint.h>

#if defined(_WIN32) && defined(__x86_64__)
/* Windows x64: rdi, rsi and xmm6-xmm15 are callee-saved too. */
typedef struct {
    uintptr_t regs[10]; /* rbx rbp rdi rsi r12 r13 r14 r15 rsp rip */
    uint32_t mxcsr;
    uint16_t fpu_cw;
    uint16_t reserved[5];
    unsigned char xmm[10][16]; /* xmm6..xmm15 */
} snap_context;
#define SNAP_CTX_SP_INDEX 8
#define SNAP_CTX_IP_INDEX 9
#else
typedef struct {
    uintptr_t regs[8]; /* x86_64: rbx rbp r12 r13 r14 r15 rsp rip; i386: ebx esi edi ebp esp eip */
    uint32_t mxcsr;
    uint16_t fpu_cw;
} snap_context;
#define SNAP_CTX_SP_INDEX (sizeof(void*) == 8 ? 6 : 4)
#define SNAP_CTX_IP_INDEX (sizeof(void*) == 8 ? 7 : 5)
#endif
#define CTX_SP(c) ((c)->regs[SNAP_CTX_SP_INDEX])
#define CTX_IP(c) ((c)->regs[SNAP_CTX_IP_INDEX])

#endif
