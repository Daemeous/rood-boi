//! The event script's arithmetic between two state flags (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

fn get(flag: c_int) c_int {
    return c.vs_battle_getStateFlag(psx.conv(i16, flag));
}

fn set(flag: c_int, value: c_int) void {
    c.vs_battle_setStateFlag(psx.conv(i16, flag), psx.conv(u8, value));
}

/// orig: vs_battle_script_setFlag (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Applies script[1]'s operator (+ - * / % & |, a copy, |a - b| or a swap) to two state flags, storing into the first.
pub fn script_setFlag(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const flag0: c_int = (@as(c_int, script[2]) >> 2) +% ((@as(c_int, script[1]) & 0xF) << 6);
    const flag1: c_int = @as(c_int, script[3]) +% ((@as(c_int, script[2]) & 3) << 8);
    // Each operator reads flag0 then flag1, as the object does.
    switch (@as(c_int, script[1]) & 0xF0) {
        0x00 => {
            const a = get(flag0);
            set(flag0, a +% get(flag1));
        },
        0x10 => {
            const a = get(flag0);
            set(flag0, a -% get(flag1));
        },
        0x20 => {
            const a = get(flag0);
            set(flag0, a *% get(flag1));
        },
        0x30 => {
            const a = get(flag0);
            set(flag0, @divTrunc(a, get(flag1)));
        },
        0x40 => {
            const a = get(flag0);
            set(flag0, @rem(a, get(flag1)));
        },
        0x50 => {
            const a = get(flag0);
            set(flag0, a & get(flag1));
        },
        0x60 => {
            const a = get(flag0);
            set(flag0, a | get(flag1));
        },
        0x70 => set(flag0, get(flag1)),
        0x80 => {
            const a = get(flag0);
            const tmp = a -% get(flag1);
            set(flag0, if (tmp < 0) -%tmp else tmp);
        },
        0x90 => {
            const prev: u8 = c.vs_battle_getStateFlag(psx.conv(i16, flag0));
            set(flag0, get(flag1));
            set(flag1, prev);
        },
        else => {},
    }
    return 0;
}
