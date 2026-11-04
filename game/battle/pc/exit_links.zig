//! Room exit links in the navigation table (D2: 73C84.s; oracle pc/src/navigation.c).
const c = @import("c_navigation");
const v = @import("views_navigation");

/// orig: func_800DC638 (pc/src/navigation.c)
/// Clears the 0x8000/0x80000000 "closed" flags of the navigation table's links
/// (0x100 words at D_800F58C0) whose room exit is active: the exits are the
/// records func_8008D2C0 fills (halfword 3 of each is a bit number), and each
/// link word is matched against the eight tile words at D_800F5910. The
/// oracle keeps the PS1 registers' mutation across iterations: `bit0` and
/// `bit1` are shifted into themselves, and `one` (the register the assembly
/// leaves 0 or 1) carries over from one iteration, and from one word, to the
/// next. A count outside 0-13 is reported (pc_unimplemented) and the oracle
/// goes on reading its stack buffer; so does this port.
pub fn enableExitLinks() callconv(.c) void {
    var exits: [13][4]u16 = undefined;
    const count = c.func_8008D2C0(&exits);
    if (count < 0 or count > 13) c.pc_unimplemented("room exit count exceeds original buffer");
    const records: [*]const [4]u16 = @ptrCast(&exits);
    var active: c_uint = 0;
    var k = count -% 1;
    while (k >= 0) : (k -%= 1) {
        active |= @as(c_uint, 1) << @as(u5, @intCast(records[@intCast(k)][3] & 31));
    }
    if (v.g.D_800F58C0().* == 0) return;
    var one: c_uint = 0;
    var one_known = false;
    var offset: c_uint = 0;
    while (offset < 0x400) : (offset += 4) {
        const entry: *align(1) c_uint = @ptrFromInt(@as(usize, v.g.D_800F58C0().* +% offset));
        var value = entry.*;
        if (value & 0x04000400 != 0) {
            value |= 0x80008000;
            const x0 = (value >> 5) & 31;
            const z0 = value & 31;
            const x1 = (value >> 21) & 31;
            const z1 = (value >> 16) & 31;
            var bit0 = (value >> 13) & 3;
            var bit1 = (value >> 29) & 3;
            var i: c_uint = 0;
            while (i < 8) : (i += 1) {
                const links: [*]align(1) const c_uint = @ptrFromInt(@as(usize, v.g.D_800F5910().*));
                const link = links[i];
                const x = link & 31;
                const z = (link >> 5) & 31;
                if (value & 0x400 != 0) {
                    one = 1;
                    one_known = true;
                    if (x0 == x) {
                        bit0 = one << @as(u5, @intCast(bit0 & 31));
                        if (z0 == z) {
                            bit0 &= active;
                            if (bit0 != 0) value &= ~@as(c_uint, 0x8000);
                        }
                    }
                }
                if (!one_known and active != 0 and value & 0x04000000 != 0)
                    c.pc_unimplemented("navigation relies on original stack address in v1");
                bit1 = one << @as(u5, @intCast(bit1 & 31));
                if (value & 0x04000000 != 0) {
                    bit1 &= active;
                    if (x1 == x and z1 == z and bit1 != 0) value &= 0x7fffffff;
                }
                one = @intFromBool(i + 1 < 8);
                one_known = true;
            }
            entry.* = value;
        }
        one = @intFromBool(offset + 4 < 0x400);
        one_known = true;
    }
}
