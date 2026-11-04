//! A pass run with a temporary heap block (D2: 7E05C.s; oracle pc/src/navigation.c).
const c = @import("c_navigation");

/// orig: func_800E6B4C (pc/src/navigation.c)
/// Takes a 0x8000-byte reverse heap block (the PS1 ran on it as its stack; the
/// host stack is used here, only the allocation order is kept), runs
/// func_800E6098 and frees the block. Nothing is run when the allocation fails.
pub fn runWithTemporaryHeap() callconv(.c) void {
    const temporary = c.vs_main_allocHeapR(0x8000) orelse return;
    c.func_800E6098();
    c.vs_main_freeHeapR(temporary);
}
