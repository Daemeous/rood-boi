//! The differential harness (pc/tools/zig_diff.py): the original C body
//! (`NAME__orig`, from the build's routed .i) and its Zig port run on the
//! same random inputs; every touched byte and every external call must agree.
//!
//! Per iteration: the state regions (zdiff.zig) get random bytes, the spec's
//! `prepare` builds the arguments (and biases the state toward edges), the
//! state is saved; the C runs, its result state and call trace are kept; the
//! state is restored and the Zig runs; then the two result states, call
//! traces and return values are compared. External functions are recording
//! stubs: each call logs its arguments (pointers into the stack as a tag, not
//! an address) and a hash of all state regions at that moment, so a write
//! moved across a call, a dropped hook or a changed argument all show.
//!
//! argv: ITERATIONS SEED. Exit 0: no mismatch; 1: mismatches; 3: the port
//! panicked; 4: a crash (a spec bug or a port fault).
//!
//! The coverage lane (`gen.coverage`, zig_diff.py --coverage) runs only the
//! C, built with gcov and the value probes of zdiff_cov.c: each call's
//! arguments are recorded, the probes are on around the C run, and nothing
//! is compared.
const std = @import("std");
const zd = @import("zdiff");
const gen = @import("gen");
const spec = @import("spec");
const common = @import("common");

pub const panic = common.Panic;

const F = @TypeOf(gen.port_fn);
const R = @typeInfo(F).@"fn".return_type.?;
const Args = std.meta.ArgsTuple(F);

extern fn printf(fmt: [*:0]const u8, ...) c_int;
extern fn fflush(stream: ?*anyopaque) c_int;
extern fn malloc(size: usize) ?[*]u8;
extern fn memcpy(dst: [*]u8, src: [*]const u8, n: usize) [*]u8;
extern fn mmap(addr: ?*anyopaque, len: usize, prot: c_int, flags: c_int, fd: c_int, off: c_long) isize;
const SigInfo = extern struct { signo: c_int, errno_: c_int, code: c_int, _pad: c_int, addr: usize };
const SigAction = extern struct {
    handler: *const fn (c_int, *const SigInfo, ?*anyopaque) callconv(.c) void,
    mask: [16]u64 = @splat(0),
    flags: c_int,
    restorer: ?*anyopaque = null,
};
const SA_SIGINFO = 4;
extern fn sigaction(sig: c_int, act: *const SigAction, old: ?*SigAction) c_int;
extern fn _exit(code: c_int) noreturn;
extern fn strtoull(s: [*:0]const u8, end: ?*?[*:0]u8, base: c_int) c_ulonglong;

// ---------------------------------------------------------------- regions
const Region = struct { base: [*]u8, size: usize, name: [*:0]const u8, random: bool };
var regions: [4]Region = undefined;
var total: usize = 0;

fn sectionRegion(comptime name: []const u8, random: bool) Region {
    const start = @extern([*]u8, .{ .name = "__start_" ++ name });
    const stop = @extern([*]u8, .{ .name = "__stop_" ++ name });
    return .{ .base = start, .size = @intFromPtr(stop) - @intFromPtr(start), .name = name ++ "", .random = random };
}

fn snapshot(buffer: [*]u8) void {
    var at: usize = 0;
    for (regions) |r| {
        _ = memcpy(buffer + at, r.base, r.size);
        at += r.size;
    }
}

fn restore(buffer: [*]const u8) void {
    var at: usize = 0;
    for (regions) |r| {
        _ = memcpy(r.base, buffer + at, r.size);
        at += r.size;
    }
}

fn hashRegions() u64 {
    var h: u64 = 0;
    for (regions) |r| h = std.hash.Wyhash.hash(h, r.base[0..r.size]);
    return h;
}

// ---------------------------------------------------------------- trace
const Entry = struct { id: u16, n: u16, args: [8]u64, state: u64 };
const trace_capacity = 4096;
var traces: [2][trace_capacity]Entry = undefined;
var trace_len: [2]usize = .{ 0, 0 };
var side: usize = 0;
var stack_top: usize = 0;
var ctx: zd.Ctx = undefined;

const stack_tag: u64 = 0x57AC_0000_0000_0000;

fn word(a: anytype) u64 {
    const T = @TypeOf(a);
    return switch (@typeInfo(T)) {
        .pointer => address(@intFromPtr(a)),
        .optional => |o| switch (@typeInfo(o.child)) {
            .pointer => if (a) |p| address(@intFromPtr(p)) else 0,
            else => @compileError("zdiff: optional " ++ @typeName(T)),
        },
        .int => |i| @as(@Int(.unsigned, i.bits), @bitCast(a)),
        .bool => @intFromBool(a),
        .float => |f| @as(@Int(.unsigned, f.bits), @bitCast(a)),
        .@"enum" => word(@intFromEnum(a)),
        else => std.hash.Wyhash.hash(0, std.mem.asBytes(&a)),
    };
}

/// A pointer into the harness's stack differs between the C and the Zig
/// frames: it is logged as a tag, its target is covered by nothing.
fn address(p: usize) u64 {
    if (p <= stack_top and stack_top - p < 8 << 20) return stack_tag;
    return p;
}

fn record(id: u16, args: anytype) void {
    const count = @typeInfo(@TypeOf(args)).@"struct".field_names.len;
    var e = Entry{ .id = id, .n = count, .args = @splat(0), .state = 0 };
    inline for (0..count) |i| {
        if (i < 8) e.args[i] = word(args[i]);
    }
    if (!@hasDecl(spec, "hash_at_calls") or spec.hash_at_calls) e.state = hashRegions();
    const n = trace_len[side];
    if (n < trace_capacity) traces[side][n] = e;
    trace_len[side] = n + 1;
}

fn defaultReturn(comptime T: type, comptime name: []const u8) T {
    return switch (@typeInfo(T)) {
        .void => {},
        .int, .bool => ctx.value(T),
        .optional => null,
        .pointer => |p| if (p.size == .c) null else @compileError("zdiff: stub " ++ name ++
            " returns a non-null pointer: give the spec a `stubs." ++ name ++ "`"),
        else => @compileError("zdiff: stub " ++ name ++ " returns " ++ @typeName(T) ++ ": give the spec a `stubs." ++ name ++ "`"),
    };
}

/// The stub's C type. An unprototyped C declaration (`short f();`) is
/// variadic in translate-c; with no fixed parameters an x86-64 call of it is
/// a call of `R f(void)` (%al, the vector-register count, is ignored).
fn StubType(comptime name: []const u8) type {
    const G = @TypeOf(@field(gen.c, name));
    const info = @typeInfo(G).@"fn";
    if (!info.attrs.varargs) return G;
    if (info.param_types.len != 0) @compileError("zdiff: " ++ name ++ " is variadic with fixed parameters: not supported");
    return fn () callconv(.c) info.return_type.?;
}

fn pointerLike(comptime T: type) bool {
    return switch (@typeInfo(T)) {
        .pointer => true,
        .optional => |o| @typeInfo(o.child) == .pointer,
        else => false,
    };
}

/// A value as the type the other side declares. The stub's type is the c
/// module's prototype (`[*c]c_UNIT.MATRIX`), a spec's stub may name the
/// same struct by its views type (`?*views_UNIT.MATRIX`): pointers to the
/// same bytes, cast here; anything else coerces as usual.
fn adapt(comptime To: type, value: anytype) To {
    const From = @TypeOf(value);
    if (From == To) return value;
    if (comptime pointerLike(From) and pointerLike(To)) return @ptrCast(@alignCast(value));
    // What would not coerce, said in the spec's terms (not as an error in this file).
    const fi = @typeInfo(From);
    const ti = @typeInfo(To);
    if ((fi == .int and ti == .int and (fi.int.signedness != ti.int.signedness or fi.int.bits > ti.int.bits)) or
        (To == void) != (From == void))
        @compileError("zdiff: a spec stub gives " ++ @typeName(From) ++ " where the C prototype has " ++ @typeName(To) ++
            ": declare the stub's return and parameter types as the c module's prototype does (or `anytype`)");
    return value;
}

/// The stub's arguments as the spec's stub declares its parameters (after
/// the `*Ctx`); an `anytype` parameter takes the argument as it is.
fn StubArgs(comptime G: type, comptime A: type) type {
    const params = @typeInfo(G).@"fn".param_types;
    const fields = @typeInfo(A).@"struct".field_types;
    var types: [fields.len]type = undefined;
    for (fields, 0..) |t, i| types[i] = if (i + 1 < params.len) (params[i + 1] orelse t) else t;
    return @Tuple(&types);
}

fn stubArgs(comptime G: type, args: anytype) StubArgs(G, @TypeOf(args)) {
    var out: StubArgs(G, @TypeOf(args)) = undefined;
    inline for (0..args.len) |i| out[i] = adapt(@TypeOf(out[i]), args[i]);
    return out;
}

/// A field of a null pointer (`&psx_get(NULL)->unk38`, an address in the
/// first page) handed to a callee: the real callee reads through it (the
/// game faults), a recording stub would not, so the run stops as the game
/// would. A null pointer itself is an argument like any other.
fn nullFieldArgs(comptime name: []const u8, args: anytype) void {
    inline for (0..args.len) |i| {
        const T = @TypeOf(args[i]);
        if (comptime pointerLike(T)) {
            const p: usize = if (@typeInfo(T) == .optional) (if (args[i]) |q| @intFromPtr(q) else 0) else @intFromPtr(args[i]);
            if (p != 0 and p < 0x1000) {
                _ = printf("zdiff: CRASH (signal 11) in the %s run, iteration %llu, fault address 0x%zx: argument %d of the call of %.*s, which reads through it\n",
                    phase, @as(c_ulonglong, iteration), p, @as(c_int, i), @as(c_int, name.len), name.ptr);
                crashTail();
            }
        }
    }
}

fn Stub(comptime id: u16, comptime name: []const u8) type {
    const G = StubType(name);
    const info = @typeInfo(G).@"fn";
    const S = info.return_type.?;
    return struct {
        fn body(args: std.meta.ArgsTuple(G)) S {
            nullFieldArgs(name, args);
            record(id, args);
            defer {
                ctx.calls += 1;
                ctx.draws = 0;
            }
            if (@hasDecl(spec, "stubs") and @hasDecl(spec.stubs, name)) {
                const f = @field(spec.stubs, name);
                return adapt(S, @call(.auto, f, .{&ctx} ++ stubArgs(@TypeOf(f), args)));
            }
            return defaultReturn(S, name);
        }
        const call = common.route.withSignature(G, body);
    };
}

comptime {
    for (gen.stubs, 0..) |name, i| @export(&Stub(i, name).call, .{ .name = name });
}

/// A trap stub (state.c) was called: a static the tool could not stub.
export fn zdiff_unexpected(name: [*:0]const u8) noreturn {
    _ = printf("zdiff: UNEXPECTED call of %s in the %s run, iteration %llu\n", name, phase, @as(c_ulonglong, iteration));
    _ = fflush(null);
    _exit(4);
}

export fn pc_psx_bad_pointer(pointer: usize) noreturn {
    _ = printf("zdiff: PANIC in the %s run, iteration %llu: psx_addr of %#zx (above 4 GB)\n", phase, @as(c_ulonglong, iteration), pointer);
    _ = fflush(null);
    _exit(3);
}

export fn pc_zig_panic(msg: [*]const u8, len: usize, return_address: usize) noreturn {
    _ = printf("zdiff: PANIC in the %s run, iteration %llu: %.*s (return address %#zx)\n", phase, @as(c_ulonglong, iteration),
        @as(c_int, @intCast(len)), msg, return_address);
    _ = fflush(null);
    _exit(3);
}

var phase: [*:0]const u8 = "setup";
var iteration: u64 = 0;

/// The iteration's arguments (as the call traces log them) and input
/// class, printed when a run crashes: which input the spec must fix.
var arg_words: [16]u64 = @splat(0);
var arg_count: usize = 0;
var class_index: ?usize = null;

fn onSignal(sig: c_int, info: *const SigInfo, _: ?*anyopaque) callconv(.c) void {
    _ = printf("zdiff: CRASH (signal %d) in the %s run, iteration %llu", sig, phase, @as(c_ulonglong, iteration));
    // SI_KERNEL: a general protection fault (a non-canonical address: no
    // fault address is reported).
    if ((sig == 11 or sig == 7) and info.code == 0x80) {
        _ = printf(", a wild pointer (a non-canonical address: random bytes used as a pointer?)");
    } else if (sig == 11 or sig == 7) _ = printf(", fault address 0x%zx", info.addr);
    _ = printf("\n");
    crashTail();
}

/// After a crash's first line: the iteration's arguments and class, then exit 4.
fn crashTail() noreturn {
    if (!std.mem.eql(u8, std.mem.span(phase), "setup") and !std.mem.eql(u8, std.mem.span(phase), "prepare")) {
        _ = printf("zdiff: crashed on arguments:");
        for (0..arg_count) |i| _ = printf(" %#llx", @as(c_ulonglong, arg_words[i]));
        if (class_count != 0) {
            if (class_index) |k| {
                if (k < class_count) _ = printf(" (class %s)", spec.classes[k].ptr);
            }
        }
        _ = printf("\n");
    }
    _ = fflush(null);
    _exit(4);
}

// ---------------------------------------------------------------- compare
var mismatches: u64 = 0;

fn report(comptime fmt: [*:0]const u8, args: anytype) void {
    mismatches += 1;
    if (mismatches > 3) return;
    _ = @call(.auto, printf, .{fmt} ++ args);
}

fn compareState(expected: [*]const u8) void {
    var at: usize = 0;
    for (regions) |r| {
        for (0..r.size) |i| {
            if (r.base[i] != expected[at + i]) {
                report("zdiff: MISMATCH iteration %llu: memory at %#zx (%s+%#zx): C %u, Zig %u\n", .{
                    @as(c_ulonglong, iteration), @intFromPtr(r.base) + i, r.name, i,
                    @as(c_uint, expected[at + i]), @as(c_uint, r.base[i]) });
                return;
            }
        }
        at += r.size;
    }
}

fn compareTraces() void {
    const a = trace_len[0];
    const b = trace_len[1];
    const n = @min(@min(a, b), trace_capacity);
    for (0..n) |i| {
        const x = traces[0][i];
        const y = traces[1][i];
        if (x.id != y.id) {
            report("zdiff: MISMATCH iteration %llu: call %zu: C calls %s, Zig calls %s\n", .{
                @as(c_ulonglong, iteration), i, stubName(x.id), stubName(y.id) });
            return;
        }
        for (0..x.n) |k| {
            if (x.args[k] != y.args[k]) {
                report("zdiff: MISMATCH iteration %llu: call %zu (%s): argument %zu: C %#llx, Zig %#llx\n", .{
                    @as(c_ulonglong, iteration), i, stubName(x.id), k, @as(c_ulonglong, x.args[k]), @as(c_ulonglong, y.args[k]) });
                return;
            }
        }
        if (x.state != y.state) {
            report("zdiff: MISMATCH iteration %llu: call %zu (%s): the state differs when it is called\n", .{
                @as(c_ulonglong, iteration), i, stubName(x.id) });
            return;
        }
    }
    if (a != b) {
        const more = if (a > b) traces[0][@min(b, trace_capacity - 1)].id else traces[1][@min(a, trace_capacity - 1)].id;
        report("zdiff: MISMATCH iteration %llu: C makes %zu external calls, Zig %zu (first unmatched: %s)\n", .{
            @as(c_ulonglong, iteration), a, b, stubName(more) });
    }
}

fn stubName(id: u16) [*:0]const u8 {
    inline for (gen.stubs, 0..) |name, i| {
        if (i == id) return name ++ "";
    }
    return "?";
}

const coverage = @hasDecl(gen, "coverage") and gen.coverage;
extern fn zdiff_cov_enable(on: c_int, stack: usize) void;
extern fn zdiff_cov_arg(index: c_int, kind: c_int, bits: c_int, value: u64) void;
extern fn zdiff_cov_dump() void;

/// One argument's value for the coverage lane's ranges (kind 0 an integer, 1 a pointer: its address).
fn coverArg(comptime i: usize, a: anytype) void {
    const T = @TypeOf(a);
    switch (@typeInfo(T)) {
        .int => |info| zdiff_cov_arg(@intCast(i), 0, info.bits, @as(@Int(.unsigned, info.bits), @bitCast(a))),
        .bool => zdiff_cov_arg(@intCast(i), 0, 1, @intFromBool(a)),
        .pointer => zdiff_cov_arg(@intCast(i), 1, 64, @intFromPtr(a)),
        .optional => zdiff_cov_arg(@intCast(i), 1, 64, if (a) |p| @intFromPtr(p) else 0),
        .@"enum" => coverArg(i, @intFromEnum(a)),
        else => {},
    }
}

const class_count = if (@hasDecl(spec, "classes")) spec.classes.len else 0;
var classes: [class_count]u64 = @splat(0);

export fn main(argc: c_int, argv: [*][*:0]u8) c_int {
    stack_top = @frameAddress();
    if (argc < 3) {
        _ = printf("usage: zdiff ITERATIONS SEED\n");
        return 2;
    }
    const iterations = strtoull(argv[1], null, 0);
    const seed = strtoull(argv[2], null, 0);
    const action = SigAction{ .handler = onSignal, .flags = SA_SIGINFO };
    for ([_]c_int{ 4, 7, 8, 11 }) |sig| _ = sigaction(sig, &action, null); // SIGILL, SIGBUS, SIGFPE, SIGSEGV
    // The PS1 scratchpad, which the decompilation addresses by number, and
    // PS1 address 0 (low RAM), where the PC C's guards read for NULL.
    if (mmap(@ptrFromInt(zd.scratchpad), zd.scratchpad_size, 3, 0x22 | 0x10, -1, 0) != @as(isize, @bitCast(zd.scratchpad))) {
        _ = printf("zdiff: cannot map the scratchpad at %#zx\n", zd.scratchpad);
        return 2;
    }
    if (mmap(@ptrFromInt(zd.low_ram), zd.low_ram_size, 3, 0x22 | 0x10, -1, 0) != @as(isize, @bitCast(zd.low_ram))) {
        _ = printf("zdiff: cannot map PS1 low RAM at %#zx\n", zd.low_ram);
        return 2;
    }
    regions = .{
        sectionRegion("zdiff_state", true),
        sectionRegion("zdiff_fixed", false),
        .{ .base = @ptrFromInt(zd.scratchpad), .size = zd.scratchpad_size, .name = "scratchpad", .random = true },
        .{ .base = @ptrFromInt(zd.low_ram), .size = zd.low_ram_size, .name = "low_ram", .random = true },
    };
    for (regions) |r| total += r.size;
    const fixed = malloc(regions[1].size + 1).?;
    _ = memcpy(fixed, regions[1].base, regions[1].size);
    const before = malloc(total).?;
    const after_c = malloc(total).?;

    var prng = std.Random.DefaultPrng.init(seed);
    const orig = @extern(*const F, .{ .name = gen.orig_name });
    var it: u64 = 0;
    while (it < iterations) : (it += 1) {
        iteration = it;
        _ = memcpy(regions[1].base, fixed, regions[1].size);
        ctx = .{ .random = prng.random(), .iteration = it, .seed = prng.random().int(u64) };
        for (regions) |r| {
            if (r.random) ctx.random.bytes(r.base[0..r.size]);
        }
        phase = "prepare";
        const args: Args = spec.prepare(&ctx);
        arg_count = @min(args.len, arg_words.len);
        inline for (0..args.len) |i| {
            if (i < arg_words.len) arg_words[i] = word(args[i]);
        }
        class_index = null;
        phase = "classify";
        if (class_count != 0) {
            const k = spec.classify(args);
            class_index = k;
            classes[k] += 1;
        }
        snapshot(before);
        const saved = ctx;
        const saved_prng = prng;

        phase = "C";
        side = 0;
        trace_len[0] = 0;
        if (coverage) {
            inline for (0..args.len) |i| coverArg(i, args[i]);
            zdiff_cov_enable(1, stack_top);
        }
        const expected: R = @call(.never_inline, orig, args);
        if (coverage) zdiff_cov_enable(0, stack_top);
        if (!coverage) {
            snapshot(after_c);

            restore(before);
            ctx = saved;
            prng = saved_prng;
            phase = "Zig";
            side = 1;
            trace_len[1] = 0;
            const actual: R = @call(.never_inline, gen.port_fn, args);
            phase = "compare";

            const before_count = mismatches;
            compareState(after_c);
            if (mismatches == before_count) compareTraces();
            if (mismatches == before_count and R != void and !std.mem.eql(u8, std.mem.asBytes(&expected), std.mem.asBytes(&actual)))
                report("zdiff: MISMATCH iteration %llu: return value\n", .{@as(c_ulonglong, iteration)});
            if (mismatches >= 1000) break;
        }
    }
    if (coverage) zdiff_cov_dump();
    _ = printf("zdiff: %s: %llu iterations, seed %llu, %llu mismatches", gen.function ++ "", @as(c_ulonglong, iteration + 1),
        @as(c_ulonglong, seed), @as(c_ulonglong, mismatches));
    if (class_count != 0) {
        _ = printf("; classes:");
        inline for (0..class_count) |i| _ = printf(" %s=%llu", spec.classes[i].ptr, @as(c_ulonglong, classes[i]));
    }
    _ = printf("\n");
    _ = fflush(null);
    return if (mismatches == 0) 0 else 1;
}
