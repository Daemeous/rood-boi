//! zig_diff spec: func_800AB2AC -> func_800AB2AC.func_800AB2AC (40564.c).
//! Inputs: the global D_800E8F2C (0, 1, 2 or 255) and an argument that
//! equals it about two times in three, or differs from it (negative,
//! 256 + the global, the global - 256, 2, 17). The 17-entry D_800F4538 table
//! holds null entries and objects whose unk183E[arg0] byte is 0xFF, 0xFE, 0
//! or 1, with a whole-table-null case. The stub for func_800AB098 writes
//! the object's unk183E byte and nulls table entries, so the loop's later
//! reads see the callee's effects.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AB2AC));

pub const classes = [_][:0]const u8{ "no-match", "match-0xFF-entry", "match-live-no-0xFF", "match-all-null" };

pub const stubs = struct {
    pub fn func_800AB098(ctx: *zd.Ctx, p: anytype, which: anytype, flag: anytype) void {
        _ = which;
        _ = flag;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) a.unk183E[ctx.value(u1)] = ctx.value(u8);
        const idx: usize = ctx.value(u8) % 17;
        if (ctx.value(u2) == 0) v.g.D_800F4538().*[idx] = null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const sel = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    v.g.D_800E8F2C().* = sel;

    var arg: c_int = sel;
    if (ctx.chance(3)) {
        const edges = [_]c_int{ -1, -256 + @as(c_int, sel), 256 + @as(c_int, sel), 2, 17 };
        arg = ctx.edge(c_int, &edges);
    }

    const table = v.g.D_800F4538();
    const all_null = ctx.chance(10);
    for (table) |*slot| {
        if (all_null or ctx.chance(5)) {
            slot.* = null;
            continue;
        }
        const obj = ctx.alloc(v.D_800F4538_t);
        slot.* = obj;
        psx.byteAt(u8, obj, 0x183E + @as(isize, arg)).* = ctx.edge(u8, &.{ 0xFF, 0xFE, 0, 1 });
    }
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    if (arg0 != @as(c_int, v.g.D_800E8F2C().*)) return 0;
    var live = false;
    for (v.g.D_800F4538().*) |p| {
        const e = p orelse continue;
        live = true;
        if (psx.byteAt(u8, e, 0x183E + @as(isize, arg0)).* == 0xFF) return 1;
    }
    return if (live) 2 else 3;
}
