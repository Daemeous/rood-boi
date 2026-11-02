//! zig_diff spec: func_800A4A24 -> func_800A4A24.func_800A4A24 (3A1A0.c).
//! Inputs: the platform id arg0 (0..12, edges around the -2 bias, any int).
//! func_8008D2C0's stub fills the table it is handed (the C's static `sp10`,
//! globalised: views func_800A4A24__sp10) with 0..4 entries whose unk6 are
//! small ids; one of them is arg0 - 2 (the first, a later one, two of them)
//! unless the class wants none. The return is the entry's address in the
//! shared static, or PS1 address 0.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A4A24));

pub const classes = [_][:0]const u8{ "empty", "first", "later", "none" };

var count: c_int = 0;
var hit: c_int = -1;
var want: i16 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = if (ctx.chance(4)) ctx.pick(c_int, &.{ 1, 2, 3, 0x10001 }) else ctx.range(c_int, 0, 12);
    count = ctx.range(c_int, 0, 4);
    want = @truncate(arg0 -% 2);
    hit = if (count == 0 or ctx.chance(3)) -1 else ctx.range(c_int, 0, count - 1);
    // The table's old contents (the static keeps the last call's) never match.
    for (v.g.func_800A4A24__sp10()) |*e| e.unk6 = want +% 100;
    return .{arg0};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (count == 0) return 0;
    if (hit == 0) return 1;
    if (hit > 0) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn func_8008D2C0(ctx: *zd.Ctx, table: anytype) c_int {
        const entries: [*]v.func_8008D2C0_t = @ptrCast(table);
        var i: c_int = 0;
        while (i < count) : (i += 1) {
            const e = &entries[@intCast(i)];
            e.unk0 = ctx.value(i16);
            e.unk2 = ctx.value(i16);
            e.unk4 = ctx.value(i16);
            // Ids near the wanted one, never it (but at `hit`, and maybe after).
            e.unk6 = if (i == hit or (hit >= 0 and i > hit and ctx.value(u2) == 0))
                want
            else
                want +% @as(i16, ctx.value(u3)) +% 1;
        }
        return count;
    }
};
