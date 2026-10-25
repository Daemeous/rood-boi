//! zig_diff spec: func_8009CAEC -> func_8009CAEC.func_8009CAEC (30DB0.c).
//! Inputs: arg1 is a row 0..3 of the three [4][16] tables. Each row holds
//! random entries and a -1 terminator at a random position, and the first
//! match is one of four kinds: no match (the `goto tail` path, 0xFF stored),
//! a match through D_800E8960[unk6E6], a direct match on unk6E6, or a table
//! match followed by a later unk6E6 entry that only the tail scan finds.
//! unk6E6 stays in 0..255 because the C indexes D_800E8960 with it. The row
//! entries are arena pointers (below 4 GB, so psx_addr accepts them). The C
//! makes no external calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009CAEC));

pub const classes = [_][:0]const u8{ "no-match", "table-match", "direct-match", "tail-after-table" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0 = ctx.alloc(v.D_800F4538_t);
    const arg1: c_int = ctx.range(c_int, 0, 3);
    const row: usize = @intCast(arg1);
    a0.unk6E6 = ctx.range(u16, 0, 255);
    const k6 = a0.unk6E6;

    const e8960 = v.g.D_800E8960();
    const tbl = &v.g.D_800F4628()[row];
    const ids = &v.g.D_800F4938()[row];
    const ptrs = &v.g.D_800F4810()[row];
    for (ptrs) |*slot| slot.* = @ptrCast(ctx.allocBytes(32, 16));
    for (ids) |*x| x.* = ctx.int(i16);

    const kind = ctx.range(u8, 0, 3);
    // kind 3 needs two entries after the table match: term >= 2.
    const term: usize = if (kind == 0)
        ctx.range(usize, 0, 15)
    else if (kind == 3)
        ctx.range(usize, 2, 15)
    else
        ctx.range(usize, 1, 15);

    // Entries before the terminator: negative, so no match can use them.
    for (tbl[0..term]) |*x| x.* = ctx.range(i16, -32768, -2);
    tbl[term] = -1;
    for (tbl[term + 1 ..]) |*x| x.* = ctx.int(i16);

    switch (kind) {
        0 => {},
        1 => {
            const k: usize = ctx.range(usize, 0, term - 1);
            e8960[k6] = ctx.int(u8);
            tbl[k] = e8960[k6];
        },
        2 => {
            const k: usize = ctx.range(usize, 0, term - 1);
            e8960[k6] = @truncate(k6 +% 1);
            tbl[k] = @intCast(k6);
        },
        else => {
            // Table match at k, the direct match at j > k, with j <= term - 1.
            const k: usize = ctx.range(usize, 0, term - 2);
            e8960[k6] = ctx.int(u8);
            tbl[k] = e8960[k6];
            const j = ctx.range(usize, k + 1, term - 1);
            tbl[j] = @intCast(k6);
        },
    }
    return .{ a0, arg1 };
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const row: usize = @intCast(args[1]);
    const tbl = &v.g.D_800F4628()[row];
    const want: i32 = v.g.D_800E8960()[a.unk6E6];
    const direct: i32 = a.unk6E6;
    var matched = false;
    var i: usize = 0;
    while (tbl[i] != -1) : (i += 1) {
        if (matched) {
            if (@as(i32, tbl[i]) == direct) return 3;
            continue;
        }
        if (@as(i32, tbl[i]) == want) {
            matched = true;
            continue;
        }
        if (@as(i32, tbl[i]) == direct) return 2;
    }
    return if (matched) 1 else 0;
}
