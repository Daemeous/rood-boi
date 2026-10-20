//! The GTE macros as the PC build runs them (PORTING.md §8): one function per
//! `gte_*` macro, doing PsyCross's calls (pc/third_party/psycross/include/psx/
//! inline_c.h, plus src/include/vs_inline_c.h for the `*2` variants) in the
//! same order, with the same register numbers and the same 32-bit loads and
//! stores. The registers are host state (`gteRegs`), compared by shadow mode.
//! Add a macro here when a port first needs it, copied from the header.

extern fn MTC2(value: c_uint, reg: c_int) callconv(.c) void;
extern fn CTC2(value: c_uint, reg: c_int) callconv(.c) void;
extern fn MFC2(reg: c_int) callconv(.c) c_uint;
extern fn doCOP2(op: c_int) callconv(.c) c_int;

/// `*(uint*)((char*)(r0)+off)`: the macros' unaligned-safe 32-bit load.
inline fn load32(r0: anytype, comptime off: usize) c_uint {
    const p: *align(1) const c_uint = @ptrFromInt(@intFromPtr(r0) + off);
    return p.*;
}

/// `*(uint*)((char*)(r0)+off) = value`.
inline fn store32(r0: anytype, comptime off: usize, value: c_uint) void {
    const p: *align(1) c_uint = @ptrFromInt(@intFromPtr(r0) + off);
    p.* = value;
}

/// gte_SetRotMatrix: the rotation part of a MATRIX into control registers 0-4.
pub inline fn SetRotMatrix(r0: anytype) void {
    CTC2(load32(r0, 0), 0);
    CTC2(load32(r0, 4), 1);
    CTC2(load32(r0, 8), 2);
    CTC2(load32(r0, 12), 3);
    CTC2(load32(r0, 16), 4);
}

/// gte_SetTransMatrix: a MATRIX's translation `t` into control registers 5-7.
pub inline fn SetTransMatrix(r0: anytype) void {
    CTC2(load32(r0, 20), 5);
    CTC2(load32(r0, 24), 6);
    CTC2(load32(r0, 28), 7);
}

/// gte_ldv0: VXY0, VZ0 from an SVECTOR.
pub inline fn ldv0(r0: anytype) void {
    MTC2(load32(r0, 0), 0);
    MTC2(load32(r0, 4), 1);
}

/// gte_rtv0tr2 (vs_inline_c.h): gte_rtv0tr on the PC.
pub inline fn rtv0tr2() void {
    _ = doCOP2(0x0480012);
}

/// gte_stlvnl: MAC1-3 (data registers 25-27) into three 32-bit words.
pub inline fn stlvnl(r0: anytype) void {
    store32(r0, 0, MFC2(25));
    store32(r0, 4, MFC2(26));
    store32(r0, 8, MFC2(27));
}
