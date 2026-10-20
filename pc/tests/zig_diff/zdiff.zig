//! The differential harness's API for a function's spec (pc/tools/zig_diff.py,
//! PORTING.md §19): the arena the spec builds inputs in, the random source,
//! and the deterministic values stubs return.
//!
//! Everything a run can touch lives in three regions the harness snapshots,
//! randomises and compares: the `zdiff_state` section (the arena below and
//! every global the function reaches, defined by the tool at its size), the
//! `zdiff_fixed` section (the unit's own data definitions, with their real
//! initial values, restored before every iteration), and the PS1 scratchpad
//! (mapped at 0x1F800000).
const std = @import("std");

pub const arena_size = 128 * 1024;
/// Inputs the spec builds (actors, stats blocks) and heap blocks stubs hand
/// out. Below 4 GB (the harness links non-PIE), so `psx.addr` accepts it.
pub export var zdiff_arena: [arena_size]u8 align(16) linksection("zdiff_state") = undefined;

pub const scratchpad: usize = 0x1F80_0000;
pub const scratchpad_size: usize = 0x1000;

pub const Ctx = struct {
    random: std.Random,
    iteration: u64,
    /// Per-iteration seed of the stubs' return values.
    seed: u64,
    /// Stub calls so far in this run (the C run and the Zig run each start at 0).
    calls: u32 = 0,
    /// Arena bytes in use.
    used: usize = 0,

    /// A fresh object in the arena (its bytes keep the random fill).
    pub fn alloc(self: *Ctx, comptime T: type) *T {
        return @ptrCast(@alignCast(self.allocBytes(@sizeOf(T), @max(@alignOf(T), 16))));
    }

    pub fn allocBytes(self: *Ctx, size: usize, alignment: usize) [*]u8 {
        const start = std.mem.alignForward(usize, self.used, alignment);
        if (start + size > arena_size) @panic("zdiff: arena exhausted (zdiff.zig arena_size)");
        self.used = start + size;
        return @ptrCast(&zdiff_arena[start]);
    }

    /// A random value of an integer type.
    pub fn int(self: *Ctx, comptime T: type) T {
        return self.random.int(T);
    }

    /// One time in `n`.
    pub fn chance(self: *Ctx, n: u32) bool {
        return self.random.uintLessThan(u32, n) == 0;
    }

    /// An edge value half the time, a random one otherwise.
    pub fn pick(self: *Ctx, comptime T: type, edges: []const T) T {
        if (edges.len != 0 and self.random.boolean()) return edges[self.random.uintLessThan(usize, edges.len)];
        return self.random.int(T);
    }

    /// A value in [low, high] (inclusive), as T.
    pub fn range(self: *Ctx, comptime T: type, low: T, high: T) T {
        return self.random.intRangeAtMost(T, low, high);
    }

    /// The value a stub returns for this call: the same for the C run and the
    /// Zig run as long as both make the same calls in the same order.
    pub fn value(self: *Ctx, comptime T: type) T {
        var z = self.seed +% @as(u64, self.calls) *% 0x9E37_79B9_7F4A_7C15;
        z = (z ^ (z >> 30)) *% 0xBF58_476D_1CE4_E5B9;
        z = (z ^ (z >> 27)) *% 0x94D0_49BB_1331_11EB;
        z ^= z >> 31;
        return switch (@typeInfo(T)) {
            .int => |i| @bitCast(@as(@Int(.unsigned, i.bits), @truncate(z))),
            .bool => z & 1 != 0,
            else => @compileError("zdiff: no default value of " ++ @typeName(T)),
        };
    }
};
