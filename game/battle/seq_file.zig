//! The animation sequence blocks of the shape (SHP), sequence (SEQ) and effect files, decoded. Each block is read in one place, here;
//! _loadSeq works from these records instead of a cursor in a global. The payload (the animation streams) stays in the file's buffer,
//! raw, where the animation decoder walks it: a record holds a pointer to it. All fields little-endian, any alignment.
//!
//! A single sequence (the base, effect, weapon, special and typed kinds differ only in what the first words mean):
//!   +0   u8 head (the base kind: animation count; the weapon kind: the weapon animation count; the typed kind: its count)
//!   +2   u16 bones (the weapon kind: the bone count the block is for, 0 for any)
//!   +4   i32 size of the data in bytes
//!   +8   the data
//! A shared sequence set (the other kinds):
//!   +0   i32 count N
//!   +4   N entries of 4 bytes: u16 animation id (+2 padding)
//!        N entries of 4 bytes: u16 key (+2 padding)
//!        N entries of 4 bytes: u32 offset of the animation's stream from the end of these tables
//!   then the streams
const std = @import("std");

pub const Block = struct {
    head: u8,
    bones: u16,
    size: c_int,
    data: [*]const u8,
};

inline fn u16At(p: [*]const u8) u16 {
    return @as(*align(1) const u16, @ptrCast(p)).*;
}
inline fn u32At(p: [*]const u8) u32 {
    return @as(*align(1) const u32, @ptrCast(p)).*;
}

pub fn block(at: usize) Block {
    const p: [*]const u8 = @ptrFromInt(at);
    return .{ .head = p[0], .bones = u16At(p + 2), .size = @bitCast(u32At(p + 4)), .data = p + 8 };
}

pub const Shared = struct {
    count: c_int,
    base: [*]const u8,

    pub fn id(self: Shared, i: c_int) u16 {
        return u16At(self.base + 4 + @as(usize, @intCast(i)) * 4);
    }
    pub fn key(self: Shared, i: c_int) u16 {
        return u16At(self.base + 4 + (@as(usize, @intCast(self.count)) + @as(usize, @intCast(i))) * 4);
    }
    pub fn offset(self: Shared, i: c_int) u32 {
        return u32At(self.base + 4 + (2 * @as(usize, @intCast(self.count)) + @as(usize, @intCast(i))) * 4);
    }
    /// The first byte after the three tables: the offsets count from here.
    pub fn streams(self: Shared) usize {
        return @intFromPtr(self.base) + 4 + 12 * @as(usize, @intCast(self.count));
    }
};

pub fn shared(at: usize) Shared {
    const p: [*]const u8 = @ptrFromInt(at);
    return .{ .count = @bitCast(u32At(p)), .base = p };
}
