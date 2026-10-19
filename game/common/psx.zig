//! Guest memory helpers for owned code (pc/docs/Ownership.md): the PS1's
//! 4-byte guest pointers, bitfields at DWARF positions, C's integer
//! conversions, and guest/host globals reached the way GCC's objects reach
//! them (PC-relative, no .refptr stubs on Windows).
const std = @import("std");
const builtin = @import("builtin");

/// A PS1-layout pointer field: the guest address in 4 bytes on every target.
pub fn Ptr(comptime T: type) type {
    return extern struct {
        raw: u32 align(4),
        pub const Target = T;
        pub fn ptr(self: @This()) ?*T {
            return if (self.raw == 0) null else @ptrFromInt(self.raw);
        }
        pub fn set(self: *@This(), p: ?*T) void {
            self.raw = addr(p);
        }
    };
}

/// A pointer as a guest address (psx_types.h psx_addr): fatal above 4 GB.
pub fn addr(p: anytype) u32 {
    const value: usize = switch (@typeInfo(@TypeOf(p))) {
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
        .@"struct" => if (@hasDecl(F, "Target")) ?*F.Target else @compileError("psx.get: " ++ @typeName(F) ++ " is not a pointer field"),
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
    var word: u64 = 0;
    inline for (0..n) |i| word |= @as(u64, bytes[b.byte + i]) << (8 * i);
    const U = @Int(.unsigned, b.width);
    return @bitCast(@as(U, @truncate(word >> b.bit)));
}

/// Store a bitfield (read-modify-write of the bytes it spans).
pub fn setBits(p: anytype, comptime name: []const u8, value: BitInt(bitSpec(@TypeOf(p), name))) void {
    const b = comptime bitSpec(@TypeOf(p), name);
    const n = comptime (b.bit + b.width + 7) / 8;
    const bytes: [*]u8 = @ptrCast(p);
    var word: u64 = 0;
    inline for (0..n) |i| word |= @as(u64, bytes[b.byte + i]) << (8 * i);
    const U = @Int(.unsigned, b.width);
    const mask: u64 = @as(u64, std.math.maxInt(U)) << b.bit;
    word = (word & ~mask) | (@as(u64, @as(U, @bitCast(value))) << b.bit);
    inline for (0..n) |i| bytes[b.byte + i] = @truncate(word >> (8 * i));
}

/// Globals reached PC-relative like GCC, never through Windows .refptr stubs.
pub inline fn global(comptime T: type, comptime name: []const u8) *T {
    return @extern(*T, .{ .name = name, .relocation = .pcrel, .visibility = .hidden });
}
pub inline fn globalMany(comptime T: type, comptime name: []const u8) [*]T {
    return @ptrCast(@extern(*T, .{ .name = name, .relocation = .pcrel, .visibility = .hidden }));
}

/// The k-th element before or after base without a bounds check.
pub inline fn at(base: anytype, k: isize) *std.meta.Elem(@TypeOf(base)) {
    const E = std.meta.Elem(@TypeOf(base));
    const first: [*]E = @ptrCast(base);
    return @ptrFromInt(@as(usize, @bitCast(@as(isize, @bitCast(@intFromPtr(first))) + k * @as(isize, @sizeOf(E)))));
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
    return @ptrCast(p);
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
