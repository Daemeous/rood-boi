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

/// gte_rtps: perspective transformation of V0.
pub inline fn rtps() void {
    _ = doCOP2(0x0180001);
}

/// gte_rtps2 (vs_inline_c.h): gte_rtps on the PC.
pub inline fn rtps2() void {
    rtps();
}

/// gte_rtpt: perspective transformation of V0-V2.
pub inline fn rtpt() void {
    _ = doCOP2(0x0280030);
}

/// gte_nclip: normal clipping (the sign of the screen triangle's area into MAC0).
pub inline fn nclip() void {
    _ = doCOP2(0x01400006);
}

/// gte_avsz3: the average of SZ1-SZ3 into OTZ.
pub inline fn avsz3() void {
    _ = doCOP2(0x0158002D);
}

/// gte_stsxy (USE_PGXP=0): SXY2 (data register 14) into a 32-bit word.
pub inline fn stsxy(r0: anytype) void {
    store32(r0, 0, MFC2(14));
}

/// gte_stotz: OTZ (data register 7) into a 32-bit word.
pub inline fn stotz(r0: anytype) void {
    store32(r0, 0, MFC2(7));
}

/// gte_stszotz: SZ3 (data register 19) >> 2 into a 32-bit word.
pub inline fn stszotz(r0: anytype) void {
    store32(r0, 0, MFC2(19) >> 2);
}

/// gte_stopz: MAC0 (data register 24) into a 32-bit word.
pub inline fn stopz(r0: anytype) void {
    store32(r0, 0, MFC2(24));
}

/// gte_ldsxy3: SXY0-SXY2 through three pointers (PsyCross's takes pointers;
/// the PS1 macro takes values), loaded in the macro's order 12, 14, 13.
pub inline fn ldsxy3(r0: anytype, r1: anytype, r2: anytype) void {
    MTC2(load32(r0, 0), 12);
    MTC2(load32(r2, 0), 14);
    MTC2(load32(r1, 0), 13);
}

/// gte_nclip2 (vs_inline_c.h): gte_nclip on the PC.
pub inline fn nclip2() void {
    nclip();
}

/// gte_zrtr (vs_inline_c.h): the translation (control registers 5-7) zeroed.
pub inline fn zrtr() void {
    CTC2(0, 5);
    CTC2(0, 6);
    CTC2(0, 7);
}
