const std = @import("std");
const route = @import("route.zig");

fn Probe(comptime F: type) type {
    return struct {
        fn sum(args: std.meta.ArgsTuple(F)) i64 {
            var result: i64 = 0;
            inline for (args, 1..) |arg, index| result += arg * @as(i64, index);
            return result;
        }
        const call = route.withSignature(F, sum);
    };
}

test "C wrappers preserve five through eight parameters and returns" {
    const F5 = fn (i64, i64, i64, i64, i64) callconv(.c) i64;
    const F6 = fn (i64, i64, i64, i64, i64, i64) callconv(.c) i64;
    const F7 = fn (i64, i64, i64, i64, i64, i64, i64) callconv(.c) i64;
    const F8 = fn (i64, i64, i64, i64, i64, i64, i64, i64) callconv(.c) i64;
    try std.testing.expectEqual(@as(i64, 55), Probe(F5).call(1, 2, 3, 4, 5));
    try std.testing.expectEqual(@as(i64, 91), Probe(F6).call(1, 2, 3, 4, 5, 6));
    try std.testing.expectEqual(@as(i64, 140), Probe(F7).call(1, 2, 3, 4, 5, 6, 7));
    try std.testing.expectEqual(@as(i64, 204), Probe(F8).call(1, 2, 3, 4, 5, 6, 7, 8));
}
