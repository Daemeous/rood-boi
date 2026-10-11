/* psx_types.h on ILP32 and LP64: PS1 layouts stay 4-byte, accessors work.
 * gcc -m32 -fno-pie -no-pie -DROOD_PC -Ipc/src pc/tests/psx_types_probe.c
 * gcc -m64 -fno-pie -no-pie -DROOD_PC -DROOD_PC_LP64 -Ipc/src pc/tests/psx_types_probe.c */
#include <stdio.h>
#include <stdlib.h>
#include "psx_types.h"

typedef struct Node {
    psx_ptr(struct Node) next;
    short x, y;
} Node;
PSX_ASSERT_SIZE(Node, 8);
PSX_ASSERT_OFFSET(Node, x, 4);

typedef struct {
    void* owner; /* runtime state: native layout */
    int value;
} State;

void pc_psx_bad_pointer(const void* pointer) {
    fprintf(stderr, "psx_types_probe: %p has no guest address\n", pointer);
    exit(1);
}

static Node nodes[2]; /* non-PIE data: below 4 GB like guest memory */

int main(void) {
    Node* last;
    psx_set(nodes[0].next, &nodes[1]);
    nodes[1].x = 7;
    last = psx_get(nodes[0].next);
    psx_copy(nodes[1].next, nodes[0].next);
    if (last != &nodes[1] || psx_get(nodes[0].next)->x != 7
        || psx_get(nodes[1].next) != &nodes[1]
        || PSX_OFFSET(State, value, 4) != (sizeof(void*) == 4 ? 4 : 8)
        || PSX_SIZE(State, 8) != sizeof(State)) {
        fprintf(stderr, "psx_types_probe: FAIL\n");
        return 1;
    }
    /* psx_postinc/psx_add/psx_set_raw behave like ++, += and a cast store. */
    {
        Node* before;
        Node* after;
        Node* raw;
        int index = 0;
        Node cursors[2];
        psx_set(cursors[0].next, &nodes[0]);
        before = psx_postinc(cursors[index++].next); /* field evaluated once */
        after = psx_add(cursors[0].next, -1);
        raw = psx_set_raw(cursors[1].next, Node*, (__UINTPTR_TYPE__)&nodes[1]);
        if (index != 1 || before != &nodes[0] || psx_get(cursors[0].next) != &nodes[0]
            || after != &nodes[0] || raw != &nodes[1] || psx_get(cursors[1].next) != &nodes[1]) {
            fprintf(stderr, "psx_types_probe: FAIL (postinc/add/set_raw)\n");
            return 1;
        }
    }
    /* psx_to_int/psx_to_ptr round-trip a guest address through a u32. */
    {
        unsigned int addr = psx_to_int(unsigned int, &nodes[1]);
        if (psx_to_ptr(Node*, addr) != &nodes[1]
            || (__UINTPTR_TYPE__)psx_to_ptr(void*, (int)0x80010000) != 0x80010000u) {
            fprintf(stderr, "psx_types_probe: FAIL (to_int/to_ptr)\n");
            return 1;
        }
    }
    /* psx_load/psx_store: a 4-byte guest pointer at a byte offset. */
    {
        unsigned char buf[12] = { 0 };
        buf[8] = 0xAA;
        psx_store(buf + 4, &nodes[1]);
        if (psx_load(Node*, buf + 4) != &nodes[1] || buf[8] != 0xAA
            || psx_load(void*, buf) != NULL) {
            fprintf(stderr, "psx_types_probe: FAIL (load/store)\n");
            return 1;
        }
    }
    printf("psx_types_probe: ok (%zu-bit, sizeof(Node) %zu)\n", sizeof(void*) * 8, sizeof(Node));
    return 0;
}
