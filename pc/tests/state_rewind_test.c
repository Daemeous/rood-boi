/* The rewind's checkpoint at the vsync boundary: a mark copies the state ranges
 * and the game stack below the boundary (snapshot.c's pc_snap_boundary); a
 * rewind restores both on its side stack, then enters the game at the mark's
 * boundary (pc_snap_enter with pc_tick_resumed). The enter is a test double
 * that checks what it would resume and returns to main. */
#include "state_diff.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uintptr_t regs[8];
    uint32_t mxcsr;
    uint16_t fpu_cw;
} snap_context;
#define CTX_SP(c) ((c)->regs[sizeof(void*) == 8 ? 6 : 4])

static char game_stack[1 << 16];
static unsigned char memory[8192];
static int resyncs, rewound, entered;
static jmp_buf back;
snap_context pc_snap_boundary;
extern int pc_snapshot_mark_point(char*, int), pc_snapshot_rewind(char*, int);
char* pc_game_stack(size_t* size) { *size = sizeof game_stack; return game_stack; }
int pc_state_ranges(pc_state_range* out, int max)
{
    if (out && max) *out = (pc_state_range){ (char*)memory, sizeof memory, "test" };
    return 1;
}
void pc_input_rewound(void) { ++resyncs; }
void pc_rpc_rewound(void) { ++rewound; }
void pc_tick_resumed(void) { }
void pc_snap_enter(const snap_context* c, void (*fn)(void))
{
    /* What a real enter would resume: the mark's boundary, the stack and state as marked. */
    assert(fn == pc_tick_resumed);
    assert(c->regs[0] == 0xb0b0 && CTX_SP(c) == (uintptr_t)(game_stack + sizeof game_stack - 256));
    assert(memory[123] == 42);
    assert((unsigned char)game_stack[sizeof game_stack - 200] == 0x5a);
    ++entered;
    longjmp(back, 1);
}

int main(void)
{
    char error[128];
    assert(pc_snapshot_rewind(error, sizeof error) == -1); /* no mark yet */
    /* A boundary off the game stack is refused. */
    CTX_SP(&pc_snap_boundary) = (uintptr_t)error;
    assert(pc_snapshot_mark_point(error, sizeof error) == -1);
    /* The boundary 256 bytes below the top of the game stack. */
    memset(&pc_snap_boundary, 0, sizeof pc_snap_boundary);
    pc_snap_boundary.regs[0] = 0xb0b0;
    CTX_SP(&pc_snap_boundary) = (uintptr_t)(game_stack + sizeof game_stack - 256);
    memory[123] = 42;
    game_stack[sizeof game_stack - 200] = 0x5a;
    assert(pc_snapshot_mark_point(error, sizeof error) == 0);
    for (int i = 0; i < 2; i++) {
        /* The game runs on: state, stack and the current boundary change. */
        memory[123] = 99;
        game_stack[sizeof game_stack - 200] = 0;
        pc_snap_boundary.regs[0] = 0xdead;
        if (!setjmp(back)) {
            pc_snapshot_rewind(error, sizeof error);
            assert(!"rewind returned instead of entering the game");
        }
    }
    assert(entered == 2 && rewound == 2 && resyncs == 2);
    puts("state_rewind: boundary mark, restored state and stack, repeated rewind and invalid calls pass");
    return 0;
}
