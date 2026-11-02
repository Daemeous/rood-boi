//! zig_diff spec: vs_battle_script_jumpBackIf -> script_jump_back_if.script_jumpBackIf (4A0A8.c).
//! Inputs: a script cursor in the arena or at PS1 address 0 (so the returned
//! pointer's 32-bit truncation is exercised), its byte 1 either 0xFF or a
//! counter index 0-15 (the C never reads past the 16-byte table), and a counter
//! at 0, 1, 2, 0x7F, 0x80, 0x81 or 0xFF (the decrement wraps and can leave the
//! sign bit set). getShort's offset is 0, -1, -32768 or any short, so the sign
//! of the negation is visible in the returned pointer.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_jumpBackIf));

pub const classes = [_][:0]const u8{ "opcode-ff", "counter-to-zero", "counter-nonzero" };

pub const stubs = struct {
    /// The C reads the offset back only through its return value; it returns
    /// the edge offsets half of the time.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return switch (ctx.value(u8) % 4) {
            0 => 0,
            1 => -1,
            2 => -32768,
            else => ctx.value(c_short),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const counters = v.g.D_800F4C10();

    var base: [*c]u8 = undefined;
    if (ctx.chance(4)) {
        base = psx.lowRam(u8);
    } else {
        base = ctx.allocBytes(16, 16);
    }

    const idx: u8 = if (ctx.chance(4)) 0xFF else ctx.range(u8, 0, 15);
    base[1] = idx;
    if (idx != 0xFF) {
        counters[idx] = ctx.edge(u8, &.{ 0, 1, 1, 2, 0x7F, 0x80, 0x81, 0xFF });
    }

    return .{ base, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const base = args[0];
    if (base[1] == 0xFF) return 0;
    const counter = v.g.D_800F4C10()[base[1]] -% 1;
    return if (counter == 0) 1 else 2;
}
