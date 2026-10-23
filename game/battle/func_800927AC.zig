//! Builds the per-entry table for a script block's ids (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800927AC (src/BATTLE/BATTLE.PRG/2842C.c)
/// Allocates one entry per script id in `arg0`'s block, fills each from its id's record and points the block at the table.
pub fn func_800927AC(arg0: ?*v.D_800F1DD8_t) callconv(.c) void {
    const a = arg0.?;
    const count: c_int = a.unk1;

    const elem_base: ?*v.func_80092F74_t = @ptrCast(@alignCast(c.vs_main_allocHeap(
        @as(c_uint, @sizeOf(v.func_80092F74_t)) *% @as(c_uint, a.unk1),
    )));

    // The id block's address is read before the field is overwritten with the table.
    var ids: usize = @intFromPtr(psx.get(a.unk4));
    psx.put(&a.unk4, @as(?*v.D_800F1DD8_t2, @ptrCast(elem_base)));
    ids +%= 4;

    var script: usize = ids +% @as(usize, a.unk1);
    if (a.unk1 % 4 != 0) {
        script = script +% (4 -% @as(usize, a.unk1 % 4));
    }

    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        const e = psx.at(elem_base.?, i);

        const id_ptr: *align(1) i8 = @ptrFromInt(ids +% @as(usize, @intCast(i)));
        e.unk8 = id_ptr.*;
        psx.put(&e.unk4, @as(?*anyopaque, @ptrFromInt(script)));

        if (e.unk8 >= 0) {
            const entry_addr: usize = @intFromPtr(c.func_8009195C(@as(c_int, e.unk8)));
            psx.put(&e.unk0, @as(?*i32, @ptrFromInt(entry_addr)));
            if (entry_addr != 0) {
                const entry: [*]align(1) i32 = @ptrFromInt(entry_addr);
                e.unkC = entry[1] *% 16;
                e.unk10 = entry[2] *% 16;
                e.unk14 = entry[3] *% 16;
                e.unk1C.vx = psx.conv(i16, entry[4]);
                e.unk1C.vy = psx.conv(i16, entry[5]);
                e.unk1C.vz = psx.conv(i16, entry[6]);
            }
        } else {
            psx.put(&e.unk0, null);
        }

        e.unk18 = 0;
        e.unk1C.pad = 0;
        if (psx.get(e.unk0) == null) {
            e.unk8 = -1;
        }
    }
}
