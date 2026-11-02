//! zig_diff spec: vs_battle_script_setFlagIfItemInInventory -> script_set_flag_if_item_in_inventory.script_setFlagIfItemInInventory (4A0A8.c).
//! Inputs: a 5-byte opcode. getShort and itemIdIsInInventory are recording
//! stubs (the item and flag ids, and the result truncated to the flag's byte:
//! 0, 1, 0x100, -1 or any).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_setFlagIfItemInInventory));

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.allocBytes(5, 1), ctx.int(c_short) };
}

pub const stubs = struct {
    pub fn vs_battle_itemIdIsInInventory(ctx: *zd.Ctx, id: c_int) c_int {
        _ = id;
        return switch (ctx.value(u8) % 5) {
            0 => 0,
            1 => 1,
            2 => 0x100,
            3 => -1,
            else => ctx.value(c_int),
        };
    }
};
