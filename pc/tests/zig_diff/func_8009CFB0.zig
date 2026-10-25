//! zig_diff spec: func_8009CFB0 -> func_8009CFB0.func_8009CFB0 (30DB0.c).
//! Inputs: an index from 0 to 16 (10 and 17 often, the `>= 0x11` and `< 10`
//! edges), or past 17 for the early return; a table slot empty or holding an
//! object with a random model id from a small edge set (0x7F, the sign-bit
//! ids 0x8000 and 0xFFFF, 0, 3), so the model-match loop finds its partner
//! or not; the object's data pointers from a small shared pool (so the
//! unk5D4 loop finds a sharer or not), a refcount walk of 0-4 with its
//! base at 20 or elsewhere, and a flag that makes the walk run. Negative
//! indices are not built: the table's predecessor in memory is not owned.
//! func_8009CC20 is a stub that rewrites the target's unk5BB and unk5B0_5,
//! as the real callee may.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009CFB0));

pub const classes = [_][:0]const u8{ "range", "empty", "no-model", "model-shared", "data-shared", "walk-refs", "plain" };

const mids = [_]u16{ 0x7F, 0x7E, 0x80, 0x8000, 0xFFFF, 3, 0 };

pub const stubs = struct {
    pub fn func_8009CC20(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = b;
        const r = ctx.value(c_int);
        const idx: usize = @intCast(a);
        if (v.g.D_800F4538()[idx]) |o| {
            if (ctx.value(u1) == 1) o.unk5BB = ctx.value(u8) % 18;
            if (ctx.value(u1) == 1) psx.setBits(o, "unk5B0_5", ctx.value(u1));
        }
        return r;
    }
};

fn makeObj(ctx: *zd.Ctx, pool: *const [3]*v.vs_battle_texInfo_t, mid: u16, d4: ?*v.vs_battle_texInfo_t) *v.D_800F4538_t {
    const o = ctx.alloc(v.D_800F4538_t);
    const blk = ctx.alloc(v.D_800F4538_unk68);
    psx.put(&blk.akaoOffset, if (ctx.chance(3)) null else pool[0]);
    psx.put(&o.unk0.unk68, blk);
    o.unk6E6 = mid;
    psx.put(&o.unk5D0, if (ctx.chance(4)) null else pool[ctx.range(usize, 0, 2)]);
    psx.put(&o.unk5D4, d4);
    return o;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const arr = v.g.D_800F4538();
    const refs = v.g.D_800F46A8();
    var r: usize = 0;
    while (r < 25) : (r += 1) {
        refs[r].refCount = if (ctx.chance(3)) 0 else ctx.int(u8);
    }
    const pool = [3]*v.vs_battle_texInfo_t{
        ctx.alloc(v.vs_battle_texInfo_t),
        ctx.alloc(v.vs_battle_texInfo_t),
        ctx.alloc(v.vs_battle_texInfo_t),
    };
    const arg0: c_int = switch (ctx.range(u8, 0, 9)) {
        0 => 10,
        1 => 17,
        2 => ctx.range(c_int, 18, 300),
        3 => 9,
        4 => 0,
        5 => 16,
        else => ctx.range(c_int, 1, 15),
    };
    const want_model = ctx.chance(2);
    const want_data = ctx.chance(2);
    var k: usize = 0;
    while (k < 17) : (k += 1) {
        if (ctx.chance(5)) {
            arr[k] = null;
            continue;
        }
        const mid: u16 = if (want_model) ctx.edge(u16, &mids) else ctx.int(u16);
        const d4: ?*v.vs_battle_texInfo_t = if (want_data and !ctx.chance(4)) pool[ctx.range(usize, 0, 2)] else null;
        arr[k] = makeObj(ctx, &pool, mid, d4);
    }
    if (arg0 < 0x11) {
        const ti: usize = @intCast(arg0);
        if (ctx.chance(8)) {
            arr[ti] = null;
        } else {
            const d4: ?*v.vs_battle_texInfo_t = if (ctx.chance(5)) null else pool[ctx.range(usize, 0, 2)];
            const t = makeObj(ctx, &pool, ctx.edge(u16, &mids), d4);
            t.unk5BB = ctx.edge(u8, &.{ 20, 20, 0, 5, 17 });
            psx.setBits(&t.unk0, "unk8_4", ctx.range(u3, 0, 4));
            psx.setBits(t, "unk5B0_5", if (ctx.chance(3)) 0 else 1);
            arr[ti] = t;
        }
    }
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    if (arg0 >= 0x11) return 0;
    const arr = v.g.D_800F4538();
    const ti: usize = @intCast(arg0);
    const t = arr[ti] orelse return 1;
    if (t.unk6E6 == 0x7F) return 2;
    var model_shared = false;
    var data_shared = false;
    for (arr, 0..) |o, i| {
        if (i == ti) continue;
        const p = o orelse continue;
        if (@as(i32, p.unk6E6) == @as(i32, @as(i16, @bitCast(t.unk6E6)))) model_shared = true;
        if (psx.rawOf(t.unk5D4) != 0 and psx.rawOf(p.unk5D4) == psx.rawOf(t.unk5D4)) data_shared = true;
    }
    if (model_shared) return 3;
    if (data_shared) return 4;
    return if (psx.getBits(t, "unk5B0_5") != 0) 5 else 6;
}
