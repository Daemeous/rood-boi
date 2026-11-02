//! The effect trail's renderer (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

fn spline(t: *v.effectTrailState, comptime side: []const u8, base: c_int, phase: c_int, out: *v.SVECTOR) void {
    const points = &@field(t, side);
    c.vs_battle_splineInterpolate(@ptrCast(&points[@intCast(base & 7)]), @ptrCast(&points[@intCast((base -% 1) & 7)]), @ptrCast(&points[@intCast((base -% 2) & 7)]), @ptrCast(&points[@intCast((base -% 3) & 7)]), phase, @ptrCast(out));
}

/// orig: func_800CE174 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// 1: a new trail (its state allocated, the config kept); 2: samples the next pair of points and, from the fourth frame, draws the trail through them in spline segments (func_800CDCBC), returning 0 when its life runs out; 3: life 8; 4: frees it (0).
pub fn func_800CE174(node_arg: [*c]v.func_800D4910_t, mode: c_uint, config: c_int) callconv(.c) c_int {
    const node: *v.func_800D4910_t = node_arg;
    var trail: ?*v.effectTrailState = @ptrCast(@alignCast(node.unk8));
    var result: c_int = 1;
    switch (mode) {
        1 => {
            trail = @ptrCast(@alignCast(c.vs_main_allocHeapR(@sizeOf(v.effectTrailState))));
            node.unk8 = @ptrCast(trail);
            trail.?.index = 0;
            trail.?.age = 0;
            trail.?.config.value = config;
        },
        2 => {
            const t = trail.?;
            t.index = (t.index +% 1) & 7;
            c.func_800A1AF8(t.config.bytes[0], t.config.bytes[1], @ptrCast(&t.first[@intCast(t.index)]), 0);
            c.func_800A1AF8(t.config.bytes[0], t.config.bytes[1], @ptrCast(&t.second[@intCast(t.index)]), 1);
            t.age +%= 1;
            if (t.age >= 4) {
                var segments: c_int = 5;
                if (t.age < 9) {
                    segments = t.age - 3;
                }
                c.SetRotMatrix(@ptrCast(&v.g.vs_scratch().viewMatrix));
                c.SetTransMatrix(@ptrCast(&v.g.vs_scratch().viewMatrix));
                spline(t, "first", t.index, -4096, &t.previousFirst);
                spline(t, "second", t.index, -4096, &t.previousSecond);
                var weight: c_int = (segments + 1) * 2; // segments 1..5
                var span: c_int = -2048;
                while (span < 0) : (span += 2048) {
                    spline(t, "first", t.index, span, &t.currentFirst);
                    spline(t, "second", t.index, span, &t.currentSecond);
                    c.func_800CDCBC(@ptrCast(t), @divTrunc(weight, 2), @divTrunc(weight - 1, 2));
                    weight -= 1;
                    t.previousFirst = t.currentFirst;
                    t.previousSecond = t.currentSecond;
                }
                span = 0;
                while (span < segments) : (span += 1) {
                    var phase: c_int = 0;
                    while (phase < 4096) : (phase += 2048) {
                        spline(t, "first", t.index -% span, phase, &t.currentFirst);
                        spline(t, "second", t.index -% span, phase, &t.currentSecond);
                        c.func_800CDCBC(@ptrCast(t), @divTrunc(weight, 2), @divTrunc(weight - 1, 2));
                        weight -= 1;
                        t.previousFirst = t.currentFirst;
                        t.previousSecond = t.currentSecond;
                    }
                }
            }
            if (t.config.bytes[3] != 255) {
                t.config.bytes[3] -%= 1;
            }
            if (t.config.bytes[3] == 0) {
                result = 0;
            }
        },
        3 => trail.?.config.bytes[3] = 8,
        4 => {
            c.vs_main_freeHeapR(trail);
            node.unk8 = null;
            result = 0;
        },
        else => {},
    }
    return result;
}
