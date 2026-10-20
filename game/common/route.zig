//! C-signature exports for the ownership trampolines.
const std = @import("std");

pub inline fn original(comptime name: []const u8, comptime F: type) *const F {
    return @extern(*const F, .{ .name = name ++ "__orig" });
}

pub fn withSignature(comptime F: type, comptime body: fn (std.meta.ArgsTuple(F)) @typeInfo(F).@"fn".return_type.?) F {
    const info = @typeInfo(F).@"fn";
    const R = info.return_type.?;
    const P = struct {
        fn t(comptime i: usize) type {
            return info.param_types[i].?;
        }
    };
    return switch (info.param_types.len) {
        0 => struct {
            fn f() callconv(.c) R {
                return body(.{});
            }
        }.f,
        1 => struct {
            fn f(a: P.t(0)) callconv(.c) R {
                return body(.{a});
            }
        }.f,
        2 => struct {
            fn f(a: P.t(0), b: P.t(1)) callconv(.c) R {
                return body(.{ a, b });
            }
        }.f,
        3 => struct {
            fn f(a: P.t(0), b: P.t(1), c: P.t(2)) callconv(.c) R {
                return body(.{ a, b, c });
            }
        }.f,
        4 => struct {
            fn f(a: P.t(0), b: P.t(1), c: P.t(2), d: P.t(3)) callconv(.c) R {
                return body(.{ a, b, c, d });
            }
        }.f,
        5 => struct {
            fn f(a: P.t(0), b: P.t(1), c: P.t(2), d: P.t(3), e: P.t(4)) callconv(.c) R {
                return body(.{ a, b, c, d, e });
            }
        }.f,
        6 => struct {
            fn f(a: P.t(0), b: P.t(1), c: P.t(2), d: P.t(3), e: P.t(4), f_: P.t(5)) callconv(.c) R {
                return body(.{ a, b, c, d, e, f_ });
            }
        }.f,
        7 => struct {
            fn f(a: P.t(0), b: P.t(1), c: P.t(2), d: P.t(3), e: P.t(4), f_: P.t(5), g: P.t(6)) callconv(.c) R {
                return body(.{ a, b, c, d, e, f_, g });
            }
        }.f,
        8 => struct {
            fn f(a: P.t(0), b: P.t(1), c: P.t(2), d: P.t(3), e: P.t(4), f_: P.t(5), g: P.t(6), h: P.t(7)) callconv(.c) R {
                return body(.{ a, b, c, d, e, f_, g, h });
            }
        }.f,
        else => @compileError("route: more than 8 parameters"),
    };
}

extern fn pc_shadow_route(name: [*:0]const u8, frame: usize,
    orig: *const fn (*anyopaque) callconv(.c) void,
    repeat: *const fn (*anyopaque) callconv(.c) void,
    owned: *const fn (*anyopaque) callconv(.c) void,
    equal: *const fn (*anyopaque) callconv(.c) c_int,
    context: *anyopaque) callconv(.c) c_int;

pub fn Shadow(comptime name: []const u8, comptime F: type, comptime owned: F) type {
    const R = @typeInfo(F).@"fn".return_type.?;
    return struct {
        const Context = struct {
            args: std.meta.ArgsTuple(F),
            expected: R = undefined,
            actual: R = undefined,
        };
        fn callOriginal(raw: *anyopaque) callconv(.c) void {
            const ctx: *Context = @ptrCast(@alignCast(raw));
            ctx.expected = @call(.never_inline, original(name, F), ctx.args);
        }
        fn callRepeat(raw: *anyopaque) callconv(.c) void {
            const ctx: *Context = @ptrCast(@alignCast(raw));
            ctx.actual = @call(.never_inline, original(name, F), ctx.args);
        }
        fn callOwned(raw: *anyopaque) callconv(.c) void {
            const ctx: *Context = @ptrCast(@alignCast(raw));
            ctx.actual = @call(.never_inline, owned, ctx.args);
        }
        fn equal(raw: *anyopaque) callconv(.c) c_int {
            if (R == void) return 1;
            const ctx: *Context = @ptrCast(@alignCast(raw));
            // Compare values, not undefined padding in struct returns.
            return @intFromBool(std.meta.eql(ctx.expected, ctx.actual));
        }
        noinline fn run(args: std.meta.ArgsTuple(F)) R {
            var ctx = Context{ .args = args };
            // The caller wrapper has no output locals. Its incoming arguments
            // and every older frame are included, never this context or guard.
            const nested_owned = pc_shadow_route(name ++ "", @frameAddress(), callOriginal, callRepeat, callOwned, equal, &ctx);
            return if (nested_owned != 0) ctx.actual else ctx.expected;
        }
        pub const call = withSignature(F, run);
    };
}

/// The exports of one subsystem's routes. `routes` is either the original
/// names (the Zig function has the same name in `Impl`) or ownership.zig's
/// `routesOf(...)`: each original `name` is exported from the Zig function at
/// `zig` (PORTING.md §16), so routing, shadow reports and hot swap keep
/// pairing by the original symbol.
pub fn exportAll(comptime subsystem: u8, comptime routes: anytype, comptime Impl: type) void {
    _ = subsystem;
    for (routes) |route| {
        const is_route = @typeInfo(@TypeOf(route)) == .@"struct";
        const name: []const u8 = if (is_route) route.name else route;
        const zig_path: []const u8 = if (is_route) route.zig else route;
        // The declaration at the dotted path ("score.updateEnemyKills").
        const f = comptime blk: {
            var space = Impl;
            var rest = zig_path;
            while (std.mem.indexOfScalar(u8, rest, '.')) |dot| {
                space = @field(space, rest[0..dot]);
                rest = rest[dot + 1 ..];
            }
            break :blk @field(space, rest);
        };
        const root = @import("root");
        const mode = if (@hasDecl(root, "reload_mode")) root.reload_mode else 0;
        if (mode != 0) {
            const Signature = struct {
                const text = (@typeName(@TypeOf(f)) ++ "\x00").*;
            };
            @export(&Signature.text, .{ .name = name ++ (if (mode == 2) "__candidate_abi" else "__owned_abi") });
        }
        if (mode == 2) {
            @export(&f, .{ .name = name ++ "__candidate" });
        } else if (mode == 1) {
            const Routed = struct {
                fn call(args: std.meta.ArgsTuple(@TypeOf(f))) @typeInfo(@TypeOf(f)).@"fn".return_type.? {
                    const target = @extern(*const @TypeOf(f), .{ .name = name ++ "__owned" });
                    return @call(.never_inline, target, args);
                }
            };
            @export(&Shadow(name, @TypeOf(f), withSignature(@TypeOf(f), Routed.call)).call,
                .{ .name = name ++ "__shadow" });
        } else {
            @export(&f, .{ .name = name ++ "__owned" });
            @export(&Shadow(name, @TypeOf(f), f).call, .{ .name = name ++ "__shadow" });
        }
    }
}
