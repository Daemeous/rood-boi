//! Works out the number of ticks an actor needs to turn to a target facing
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AA8D0 (src/BATTLE/BATTLE.PRG/40564.c)
/// Takes the shortest signed turn from actor `index`'s facing to `angle`, then passes it with the ticks at `rate` to func_800AA984.
pub fn func_800AA8D0(index: c_int, angle: c_short, rate: c_int) callconv(.c) void {
    // The C does not bound the index: psx.at keeps the unchecked element address.
    const actor = (psx.at(v.g.D_800F4538(), index).* orelse return);

    var gap: i16 = psx.conv(i16, @as(c_int, angle) -% @as(c_int, actor.unk0.facing));

    if (@as(c_int, gap) > 4096 / 2) {
        gap = psx.conv(i16, @as(c_int, gap) -% 4096);
    }
    if (@as(c_int, gap) < -4096 / 2) {
        gap = psx.conv(i16, @as(c_int, gap) +% 4096);
    }

    var magnitude: c_int = @as(c_int, gap);
    magnitude = if (magnitude >= 0) magnitude else -%magnitude;

    var divisor: c_int = rate;
    divisor = if (divisor >= 0) divisor else -%divisor;

    // divisor is 0 only when the C divides by zero: the trap is the same stop.
    var ticks: c_int = @divTrunc(magnitude, divisor);
    if (@rem(magnitude, divisor) != 0) {
        ticks +%= 1;
    }

    c.func_800AA984(index, gap, ticks);
}
