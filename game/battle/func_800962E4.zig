//! Returns the slot for the next display text, shifting the table down when it is full (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800962E4 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Returns the slot for the next display text; when all 32 slots are used, drops the oldest and returns the last.
pub fn func_800962E4() callconv(.c) ?*anyopaque {
    const base: ?*anyopaque = psx.get(v.g.D_800F1BAC().*);
    const table: [*c]align(1) v.D_800F1BAC_t = @ptrCast(base);
    const size = @sizeOf(v.D_800F1BAC_t);

    if (v.g.D_800F227E().* < 32) {
        return @ptrCast(psx.at(table, v.g.D_800F227E().*));
    }

    var i: c_int = 0;
    while (i < 31) : (i +%= 1) {
        c.vs_main_memcpy(
            @ptrCast(psx.at(table, i)),
            @ptrCast(psx.at(table, i +% 1)),
            size,
        );
    }
    c.vs_main_bzero(@ptrCast(psx.at(table, 31)), size);
    return @ptrCast(psx.at(table, 31));
}
