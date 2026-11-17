//! Guest memory helpers for owned code (pc/docs/Ownership.md): the PS1's
//! 4-byte guest pointers, bitfields at DWARF positions, C's integer
//! conversions, and guest/host globals reached the way GCC's objects reach
//! them (PC-relative, no .refptr stubs on Windows).
const std = @import("std");
const builtin = @import("builtin");

/// A guest address is only 4-aligned: a pointer to a type aligned above 4
/// (64-bit structs with 8-byte members) is `*align(4) T` (PORTING.md §20).
pub fn GuestPtr(comptime T: type) type {
    if (@typeInfo(T) == .@"opaque") return *T;
    return if (@alignOf(T) > 4) *align(4) T else *T;
}

/// A PS1-layout pointer field: the guest address in 4 bytes on every target.
pub fn Ptr(comptime T: type) type {
    return extern struct {
        raw: u32 align(4),
        pub const Target = T;
        pub fn ptr(self: @This()) ?GuestPtr(T) {
            return if (self.raw == 0) null else @ptrFromInt(self.raw);
        }
        pub fn set(self: *@This(), p: ?GuestPtr(T)) void {
            self.raw = addr(p);
        }
    };
}

/// A pointer as a guest address (psx_types.h psx_addr): fatal above 4 GB.
pub fn addr(p: anytype) u32 {
    const value: usize = switch (@typeInfo(@TypeOf(p))) {
        .null => 0, // a literal `null` (psx_set(field, NULL))
        .optional => if (p) |q| @intFromPtr(q) else 0,
        else => @intFromPtr(p),
    };
    if (comptime @sizeOf(usize) > 4) {
        if (value > std.math.maxInt(u32)) badPointer(value);
    }
    return @truncate(value);
}

extern fn pc_psx_bad_pointer(pointer: usize) callconv(.c) noreturn;
fn badPointer(value: usize) noreturn {
    pc_psx_bad_pointer(value);
}

fn GetResult(comptime F: type) type {
    return switch (@typeInfo(F)) {
        .optional, .pointer => F,
        .@"struct" => if (@hasDecl(F, "Target")) ?GuestPtr(F.Target) else @compileError("psx.get: " ++ @typeName(F) ++ " is not a pointer field"),
        else => @compileError("psx.get: " ++ @typeName(F) ++ " is not a pointer field"),
    };
}

/// psx_get: a native pointer or a 4-byte guest pointer's target.
pub fn get(field: anytype) GetResult(@TypeOf(field)) {
    return switch (@typeInfo(@TypeOf(field))) {
        .optional, .pointer => field,
        else => field.ptr(),
    };
}

/// C's integer conversion: narrow modulo 2^bits, widen by source signedness.
pub fn conv(comptime T: type, x: anytype) T {
    const X = @TypeOf(x);
    const xi = @typeInfo(X).int;
    const ti = @typeInfo(T).int;
    const UX = @Int(.unsigned, xi.bits);
    const UT = @Int(.unsigned, ti.bits);
    if (ti.bits <= xi.bits) {
        return @bitCast(@as(UT, @truncate(@as(UX, @bitCast(x)))));
    }
    const Wide = @Int(xi.signedness, ti.bits);
    return @bitCast(@as(Wide, x));
}

pub const Bit = struct { byte: usize, bit: u3, width: u16, signed: bool };

fn BitInt(comptime b: Bit) type {
    return @Int(if (b.signed) .signed else .unsigned, b.width);
}
fn bitSpec(comptime P: type, comptime name: []const u8) Bit {
    const S = @typeInfo(P).pointer.child;
    return @field(S.bitfields, name);
}

/// Read a bitfield at its DWARF position.
pub fn getBits(p: anytype, comptime name: []const u8) BitInt(bitSpec(@TypeOf(p), name)) {
    const b = comptime bitSpec(@TypeOf(p), name);
    const n = comptime (b.bit + b.width + 7) / 8;
    const bytes: [*]const u8 = @ptrCast(p);
    const W = Word(n);
    var word: W = 0;
    inline for (0..n) |i| word |= @as(W, bytes[b.byte + i]) << (8 * i);
    const U = @Int(.unsigned, b.width);
    return @bitCast(@as(U, @truncate(word >> b.bit)));
}

/// The bytes a bitfield spans, in the narrowest word that holds them: with a u64 for a 4-byte field,
/// Zig 0.17's LLVM on i686 widened the truncated value back without its mask (a u26 `flags` kept the
/// next field's bits; battle 32-bit, func_800D2ADC).
fn Word(comptime n: usize) type {
    return if (n <= 4) u32 else u64;
}

/// Store a bitfield (read-modify-write of the bytes it spans).
pub fn setBits(p: anytype, comptime name: []const u8, value: BitInt(bitSpec(@TypeOf(p), name))) void {
    const b = comptime bitSpec(@TypeOf(p), name);
    const n = comptime (b.bit + b.width + 7) / 8;
    const bytes: [*]u8 = @ptrCast(p);
    const W = Word(n);
    var word: W = 0;
    inline for (0..n) |i| word |= @as(W, bytes[b.byte + i]) << (8 * i);
    const U = @Int(.unsigned, b.width);
    const mask: W = @as(W, std.math.maxInt(U)) << b.bit;
    word = (word & ~mask) | (@as(W, @as(U, @bitCast(value))) << b.bit);
    inline for (0..n) |i| bytes[b.byte + i] = @truncate(word >> (8 * i));
}

/// The development library (hot swap, reload_mode 2) reaches the host's
/// globals through its GOT: a hidden PC-relative reference cannot leave a
/// shared object (PORTING.md §20, hot swap item 2). Everything else keeps
/// GCC's PC-relative access.
pub const library = blk: {
    const root = @import("root");
    break :blk @hasDecl(root, "reload_mode") and root.reload_mode == 2;
};

/// The development host (reload_mode 1) holds the resident routes' code: like the library, it reaches the
/// globals through its GOT. A hidden reference would make the linker keep each global local, and the
/// library could not find it.
pub const host = blk: {
    const root = @import("root");
    break :blk @hasDecl(root, "reload_mode") and root.reload_mode == 1;
};

/// A global reached PC-relative like GCC, never through Windows .refptr stubs: `psx.Global(T, "name").ptr()`.
/// A type, not a function taking the name: Zig's Debug code (the development library) spills an inline
/// function's comptime string parameter into the caller's frame, and the unwritten slot then keeps a host
/// pointer (into the game library's rodata) on the game stack, which is saved state. A name captured by a
/// type reaches no frame. An unsized array (`extern T name[];`) is `GlobalMany(T, "name")`, declared as
/// `T name[0]`, as the C declares it, never as one `T`: the 64-bit build sizes each moved pointer import
/// from every object's declaration, and a complete one-element declaration shrank `vs_battle_actors`
/// (16 slots) to its first two.
pub fn Global(comptime T: type, comptime name: []const u8) type {
    return struct {
        pub inline fn ptr() *T {
            if (library or host) return @extern(*T, .{ .name = name });
            return @extern(*T, .{ .name = name, .relocation = .pcrel, .visibility = .hidden });
        }
    };
}

/// An unsized array (`extern T name[];`) as a many-item pointer: `psx.GlobalMany(T, "name").ptr()`.
pub fn GlobalMany(comptime T: type, comptime name: []const u8) type {
    return struct {
        pub inline fn ptr() [*]T {
            return Global([0]T, name).ptr();
        }
    };
}

/// A host hook the C declares `__attribute__((weak))` and tests for null
/// (PORTING.md §12): the address is laundered through a register so the
/// test is not folded to "present". Not through memory: a frame that lives
/// across the vsync (battle_frame) would keep an engine address on the game
/// stack, which differs between engine builds (snapshot_resume_smoke).
pub inline fn weak(comptime F: type, comptime name: []const u8) ?F {
    // COFF: LLVM reaches a weak external only through a .refptr stub, which
    // the build rejects (overlay renames cannot follow it), where MinGW's C
    // uses a PC-relative lea. Every Windows build links the whole game, so
    // the hooks are always defined there: a strong PC-relative reference, the
    // same call when present, a link error (not a skipped hook) if one is not.
    const f: ?F = if (builtin.os.tag == .windows)
        @extern(F, .{ .name = name, .relocation = .pcrel, .visibility = .hidden })
    else
        @extern(?F, .{ .name = name, .linkage = .weak });
    return @ptrFromInt(asm volatile (""
        : [out] "=r" (-> usize),
        : [in] "0" (@intFromPtr(f)),
    ));
}

/// psx_set on a pointer field: a native pointer field takes the pointer, a
/// psx_ptr field its guest address (psx_addr: fatal above 4 GB).
pub fn put(field: anytype, p: anytype) void {
    const F = @typeInfo(@TypeOf(field)).pointer.child;
    if (@TypeOf(p) == @TypeOf(null)) {
        switch (@typeInfo(F)) {
            .optional => field.* = null,
            .pointer => @compileError("psx.put: null into a non-optional pointer field"),
            else => field.raw = 0,
        }
        return;
    }
    switch (@typeInfo(F)) {
        .optional, .pointer => field.* = @ptrCast(@alignCast(p)),
        else => field.raw = addr(p),
    }
}

/// psx_copy: one pointer field into another as stored, never made a
/// pointer on the way (a stale guest link need not be aligned).
pub fn copy(dst: anytype, src: anytype) void {
    switch (@typeInfo(@TypeOf(src))) {
        .optional, .pointer => dst.* = src,
        else => dst.raw = src.raw,
    }
}

/// psx_raw: a psx_ptr field's 4-byte guest address, a native field's address.
pub fn rawOf(field: anytype) u32 {
    return switch (@typeInfo(@TypeOf(field))) {
        .optional, .pointer => addr(field),
        else => field.raw,
    };
}

/// Where the host maps the PS1's 2 MB of main RAM (T12; psx_types.h ROOD_GUEST_RAM_BASE).
pub const guest_ram_base: u32 = 0x80000000;

/// The 64 KiB of zeroes (rood_null_page, boot_runtime.c) where the PC C's
/// safety guards read instead of NULL: the original read PS1 address 0
/// (PORTING.md §5.3). One page, shared with the C.
var test_null_page: [0x10000]u8 align(16) = std.mem.zeroes([0x10000]u8);
pub inline fn lowRam(comptime T: type) *align(1) T {
    return @ptrCast(if (builtin.is_test) &test_null_page else Global([0x10000]u8, "rood_null_page").ptr());
}

/// Struct names, one per line (or `raw`: base types of one byte, void, integers), that `byteAt` rejects at
/// compile time. Empty except while pc/tools/pointer_sites.py runs: it lists every site that reaches a struct
/// by byte offset, which a changed layout would read wrongly without any compile error.
const audit = @embedFile("psx_audit.txt");

fn auditBase(comptime Base: type) void {
    @setEvalBranchQuota(100_000);
    const T = switch (@typeInfo(Base)) {
        .pointer => |p| p.child,
        .optional => |o| switch (@typeInfo(o.child)) {
            .pointer => |p| p.child,
            else => o.child,
        },
        else => Base,
    };
    const name = @typeName(T);
    const is_raw = switch (@typeInfo(T)) {
        .int => true,
        .@"opaque" => true,
        .array => |a| @typeInfo(a.child) == .int,
        else => false,
    };
    var lines = std.mem.tokenizeScalar(u8, audit, '\n');
    while (lines.next()) |line| {
        const hit = if (std.mem.eql(u8, line, "raw"))
            is_raw
        else
            std.mem.eql(u8, name, line) or std.mem.endsWith(u8, name, "." ++ line) or
                std.mem.endsWith(u8, name, ".struct_" ++ line);
        if (hit) @compileError("byteAt: base " ++ name);
    }
}

/// `(T*)((char*)base + off)`: an unaligned view at a byte offset.
pub inline fn byteAt(comptime T: type, base: anytype, off: isize) *align(1) T {
    if (comptime audit.len != 0) comptime auditBase(@TypeOf(base));
    return @ptrFromInt(@intFromPtr(base) +% @as(usize, @bitCast(off)));
}

/// `(T*)((char*)field + off)`: a view at an offset from a named field of a pointer-free struct, for words that
/// span or overlay its members (a bitfield run, a table inside an array). The anchor moves with a layout change.
pub inline fn fieldAt(comptime T: type, field: anytype, off: isize) *align(1) T {
    return @ptrFromInt(@intFromPtr(field) +% @as(usize, @bitCast(off)));
}

/// A word of a bitfield group (`_bits_*`): a T at byte `off` of the group, bounds-checked against the group's bytes at
/// compile time (a read past the group is an error, which `fieldAt` accepted silently).
pub inline fn bitsAt(comptime T: type, group: anytype, comptime off: usize) *align(1) T {
    const A = @typeInfo(@TypeOf(group)).pointer.child;
    comptime {
        const info = @typeInfo(A);
        if (info != .array or info.array.child != u8 or off + @sizeOf(T) > info.array.len)
            @compileError("psx.bitsAt: a " ++ @typeName(T) ++ " at " ++ std.fmt.comptimePrint("{d}", .{off}) ++ " does not fit " ++ @typeName(A));
    }
    return @ptrCast(@as([*]u8, @ptrCast(group)) + off);
}

/// C's `long` in game code: 4 bytes on 32-bit, 8 on 64-bit Linux and on the
/// Windows twin (whose units have `long` respelled `long long`).
pub const Long = if (@sizeOf(usize) == 8) i64 else i32;

fn AtResult(comptime P: type) type {
    const info = @typeInfo(P).pointer;
    const E = if (info.size == .one) switch (@typeInfo(info.child)) {
        .array => |a| a.child,
        else => info.child,
    } else info.child;
    var attrs = info.attrs;
    attrs.@"allowzero" = false;
    // &base[k] is aligned to the base only as far as k * @sizeOf(E) keeps it:
    // the element's alignment is min(base alignment, the largest power of two
    // dividing @sizeOf(E)). A `*align(4) [6]u16` base gives `*align(2) u16`
    // (its odd elements sit at 2 mod 4), never `*align(4) u16`.
    const base_align = info.attrs.@"align" orelse @alignOf(info.child);
    const size = @sizeOf(E);
    const step: usize = if (size == 0) base_align else @as(usize, 1) << @ctz(@as(usize, size));
    const element_align = @min(base_align, step);
    attrs.@"align" = if (element_align == @alignOf(E)) null else element_align;
    return @Pointer(.one, attrs, E, null);
}

/// `&base[k]` as C computes it: no bounds check, wrapping address arithmetic,
/// the index converted as C converts it (a signed index sign-extends, an
/// unsigned one zero-extends), the base's constness kept and its alignment
/// kept as far as the element offset preserves it (AtResult).
/// `base` is a `*T`, `*[N]T`, `[*]T` or `[*c]T`.
pub inline fn at(base: anytype, k: anytype) AtResult(@TypeOf(base)) {
    const R = AtResult(@TypeOf(base));
    const E = @typeInfo(R).pointer.child;
    const K = @TypeOf(k);
    const index: usize = switch (@typeInfo(K)) {
        .comptime_int => @bitCast(@as(isize, k)),
        .int => |i| if (i.signedness == .signed) @bitCast(@as(isize, k)) else @as(usize, k),
        else => @compileError("psx.at: index must be an integer"),
    };
    return @ptrFromInt(@intFromPtr(base) +% index *% @sizeOf(E));
}

/// View to C pointer, checked at comptime for matching type name and size.
pub fn cptr(comptime CT: type, p: anytype) [*c]CT {
    const VT = @typeInfo(@TypeOf(p)).pointer.child;
    comptime {
        const vn = @typeName(VT);
        const cn = @typeName(CT);
        const vs = vn[(std.mem.lastIndexOfScalar(u8, vn, '.') orelse 0)..];
        const cs = cn[(std.mem.lastIndexOfScalar(u8, cn, '.') orelse 0)..];
        if (!std.mem.eql(u8, vs, cs) and !std.mem.eql(u8, vs, ".struct_" ++ cs[1..]) and !std.mem.eql(u8, ".struct_" ++ vs[1..], cs))
            @compileError("psx.cptr: " ++ vn ++ " is not " ++ cn);
        if (@typeInfo(CT) != .@"opaque" and @sizeOf(CT) != @sizeOf(VT))
            @compileError("psx.cptr: " ++ vn ++ " and " ++ cn ++ " differ in size");
    }
    // A guest pointer type is only 4-aligned (GuestPtr); the C type may need more.
    const have = @typeInfo(@TypeOf(p)).pointer.attrs.@"align" orelse @alignOf(VT);
    const need = if (@typeInfo(CT) == .@"opaque") 1 else @alignOf(CT);
    return if (comptime have >= need) @ptrCast(p) else @ptrCast(@alignCast(p));
}

test "Ptr is 4 bytes and round-trips a low address" {
    const Node = extern struct { next: Ptr(u32), x: i16, y: i16 };
    try std.testing.expectEqual(@as(usize, 8), @sizeOf(Node));
    try std.testing.expectEqual(@as(usize, 4), @offsetOf(Node, "x"));
    var n = Node{ .next = .{ .raw = 0 }, .x = 1, .y = 2 };
    try std.testing.expect(get(n.next) == null);
    n.next.raw = 0x1000;
    try std.testing.expectEqual(@as(usize, 0x1000), @intFromPtr(get(n.next).?));
}

test "get passes native pointers through" {
    var x: u32 = 5;
    const p: ?*u32 = &x;
    try std.testing.expectEqual(p, get(p));
}

test "conv is C's integer conversion" {
    try std.testing.expectEqual(@as(u16, 0xFFFE), conv(u16, @as(c_int, -2)));
    try std.testing.expectEqual(@as(i8, -1), conv(i8, @as(u32, 0x1FF)));
    try std.testing.expectEqual(@as(u32, 0xFFFF_FFFF), conv(u32, @as(i8, -1)));
    try std.testing.expectEqual(@as(c_int, 200), conv(c_int, @as(u8, 200)));
}

test "at: no bounds, signed and unsigned indices, wrapping, alignment kept" {
    var a = [_]i16{ 10, 11, 12, 13 };
    const many: [*]i16 = &a;
    try std.testing.expectEqual(@as(i16, 13), at(many, @as(u32, 3)).*);
    try std.testing.expectEqual(@as(i16, 11), at(at(many, 3), @as(c_int, -2)).*);
    try std.testing.expectEqual(@as(i16, 12), at(&a[0], 2).*);
    try std.testing.expectEqual(@as(i16, 12), at(&a, @as(u8, 2)).*);
    const loose: *align(1) i16 = @ptrCast(&a[1]);
    const r = at(loose, @as(c_int, 1));
    try std.testing.expect(@typeInfo(@TypeOf(r)).pointer.attrs.@"align" == 1);
    try std.testing.expectEqual(@as(i16, 12), r.*);
    // A guest address near 0x80000000 with a negative index wraps like C.
    const guest: [*]align(1) u8 = @ptrFromInt(0x8000_0010);
    try std.testing.expectEqual(@as(usize, 0x8000_0000), @intFromPtr(at(guest, @as(c_int, -16))));
}

fn alignOfResult(comptime P: type) usize {
    const a = @typeInfo(AtResult(P)).pointer.attrs.@"align";
    return a orelse @alignOf(@typeInfo(AtResult(P)).pointer.child);
}

test "at: an over-aligned base gives the element its own alignment" {
    // The score data's kill counts: `*align(4) [6]u16`, odd entries at 2 mod 4.
    const Score = extern struct { flags: u32, kills: [6]u16, tail: u32 };
    var s: Score align(8) = .{ .flags = 0, .kills = .{ 0, 0, 0, 0, 0, 0 }, .tail = 0 };
    const kills: *align(4) [6]u16 = @alignCast(&s.kills);
    try std.testing.expectEqual(@as(usize, 2), alignOfResult(@TypeOf(kills)));
    inline for (0..8) |k| at(kills, @as(c_int, k)).* +%= 1; // 6 and 7 run into `tail`, as in C
    try std.testing.expectEqual([6]u16{ 1, 1, 1, 1, 1, 1 }, s.kills);
    try std.testing.expectEqual(@as(u32, 0x0001_0001), s.tail);
    // u8 elements of an 8-aligned base: byte alignment.
    var bytes: [16]u8 align(8) = @splat(0);
    const b8: *align(8) [16]u8 = &bytes;
    try std.testing.expectEqual(@as(usize, 1), alignOfResult(@TypeOf(b8)));
    at(b8, @as(u32, 7)).* = 9;
    try std.testing.expectEqual(@as(u8, 9), bytes[7]);
    // An odd-size element (3 bytes, align 1) and a 6-byte element of u16s
    // (align 2 on any base of 2 or more).
    const Rgb = extern struct { r: u8, g: u8, b: u8 };
    var rgb: [4]Rgb align(4) = undefined;
    const prgb: *align(4) [4]Rgb = &rgb;
    try std.testing.expectEqual(@as(usize, 1), alignOfResult(@TypeOf(prgb)));
    at(prgb, @as(c_int, 1)).* = .{ .r = 1, .g = 2, .b = 3 };
    try std.testing.expectEqual(@as(u8, 2), rgb[1].g);
    const Vec = extern struct { x: i16, y: i16, z: i16 };
    var vecs: [3]Vec align(16) = undefined;
    const pvec: [*]align(16) Vec = &vecs;
    try std.testing.expectEqual(@as(usize, 2), alignOfResult(@TypeOf(pvec)));
    at(pvec, @as(u8, 1)).* = .{ .x = 4, .y = 5, .z = 6 };
    try std.testing.expectEqual(@as(i16, 5), vecs[1].y);
    // A u32 element of an 8-aligned base stays 4-aligned; a natural base keeps
    // the element's natural alignment.
    var words: [4]u32 align(8) = .{ 0, 0, 0, 0 };
    const pw: *align(8) [4]u32 = &words;
    try std.testing.expectEqual(@as(usize, 4), alignOfResult(@TypeOf(pw)));
    at(pw, @as(c_int, 3)).* = 7;
    try std.testing.expectEqual(@as(u32, 7), words[3]);
    try std.testing.expectEqual(@as(usize, @alignOf(u32)), alignOfResult(*[4]u32));
    try std.testing.expect(@typeInfo(AtResult(*[4]u32)).pointer.attrs.@"align" == null);
}

test "put and rawOf store a pointer field the C way" {
    const Node = extern struct { next: Ptr(u32), native: ?*u32 };
    var x: u32 = 1;
    var n = Node{ .next = .{ .raw = 0 }, .native = null };
    const guest: *u32 = @ptrFromInt(0x1000);
    put(&n.next, guest);
    put(&n.native, &x);
    try std.testing.expectEqual(@as(u32, 0x1000), rawOf(n.next));
    try std.testing.expectEqual(&x, n.native.?);
    put(&n.next, @as(?*u32, null));
    try std.testing.expectEqual(@as(u32, 0), n.next.raw);
    // A literal null, as a spec writes psx_set(field, NULL) (wave 2's 73700 specs).
    put(&n.next, guest);
    put(&n.next, null);
    put(&n.native, null);
    try std.testing.expectEqual(@as(u32, 0), n.next.raw);
    try std.testing.expectEqual(@as(?*u32, null), n.native);
    try std.testing.expectEqual(@as(u32, 0), addr(null));
}

test "byteAt and lowRam are unaligned views" {
    var bytes = [_]u8{ 0, 0x34, 0x12, 0 };
    try std.testing.expectEqual(@as(u16, 0x1234), byteAt(u16, &bytes, 1).*);
    try std.testing.expectEqual(@as(u32, 0), lowRam(u32).*);
}

test "weak: an absent hook is null at run time" {
    const Hook = *const fn (c_int) callconv(.c) void;
    try std.testing.expect(weak(Hook, "rood_psx_test_absent_hook") == null);
}

test "bitfields at DWARF positions" {
    const T = extern struct {
        id: u8,
        _bits_1: [2]u8,
        pub const bitfields = .{
            .flag = Bit{ .byte = 1, .bit = 0, .width = 1, .signed = false },
            .kind = Bit{ .byte = 1, .bit = 1, .width = 3, .signed = false },
            .wide = Bit{ .byte = 1, .bit = 4, .width = 9, .signed = false },
            .neg = Bit{ .byte = 2, .bit = 5, .width = 3, .signed = true },
        };
    };
    var t = T{ .id = 0, ._bits_1 = .{ 0, 0 } };
    setBits(&t, "flag", 1);
    setBits(&t, "kind", 5);
    setBits(&t, "wide", 0x1A5);
    try std.testing.expectEqual(@as(u1, 1), getBits(&t, "flag"));
    try std.testing.expectEqual(@as(u3, 5), getBits(&t, "kind"));
    try std.testing.expectEqual(@as(u9, 0x1A5), getBits(&t, "wide"));
    setBits(&t, "neg", -2);
    try std.testing.expectEqual(@as(i3, -2), getBits(&t, "neg"));
    try std.testing.expectEqual(@as(u9, 0x1A5 & 0x1FF), getBits(&t, "wide") & 0x1FF);
}
