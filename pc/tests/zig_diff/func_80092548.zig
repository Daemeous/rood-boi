//! zig_diff spec: func_80092548 -> func_80092548.func_80092548 (2842C.c).
//! Inputs: the room's texture-effects section (valid records of kinds 0, 1,
//! 2 and 6, ended either by a record of an invalid kind or by the length
//! exactly, and a non-positive length) and its section D (up to 8 items with
//! counts 0-8, so both alignment branches; active or not; chunk lists with
//! ordinary and type-11 chunks; a length that is exact, partial, or
//! non-positive). func_800927AC rewrites the entry's count, which the C
//! re-reads for the item's size, and its unk3.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092548));

// texture state (0 skip, 1 ends on a bad kind, 2 ends at the length) times
// section D state (0 skip, 1 partial length, 2 exact length)
pub const classes = [_][:0]const u8{
    "tex-skip sec-skip",
    "tex-skip sec-partial",
    "tex-skip sec-exact",
    "tex-kind sec-skip",
    "tex-kind sec-partial",
    "tex-kind sec-exact",
    "tex-len sec-skip",
    "tex-len sec-partial",
    "tex-len sec-exact",
};

var class_index: usize = 0;

pub const stubs = struct {
    pub fn func_800927AC(ctx: *zd.Ctx, entry: anytype) void {
        const e: *v.D_800F1DD8_t = @ptrCast(@alignCast(entry));
        // The count (unk1) is re-read by the C for the item's size; it stays
        // at most 127 so the walk over section D stays bounded.
        if (ctx.value(u1) == 1) e.unk1 = ctx.value(u8) & 0x7F;
        if (ctx.value(u1) == 1) e.unk3 = ctx.value(u8);
    }
};

fn recordSize(kind: u8) usize {
    return switch (kind) {
        0 => 24,
        1 => 20,
        2 => 32,
        6 => 36,
        else => 0,
    };
}

/// Writes a texture-effects record header (unk5, unk6) at pos; returns the next record's offset.
fn putRecord(ctx: *zd.Ctx, buf: [*]u8, pos: usize, kind: u8) usize {
    buf[pos + 5] = if (ctx.chance(2)) 0 else ctx.int(u8);
    buf[pos + 6] = kind;
    return pos + recordSize(kind);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();

    // Texture-effects section.
    const tex_state = ctx.range(usize, 0, 2);
    const tex = ctx.allocBytes(2048, 16);
    var tex_pos: usize = 0;
    var tex_len: u32 = 0;
    switch (tex_state) {
        0 => tex_len = ctx.edge(u32, &.{ 0, 0x8000_0000, 0xFFFF_FFFF }),
        1 => {
            for (0..ctx.range(usize, 0, 10)) |_| {
                tex_pos = putRecord(ctx, tex, tex_pos, ctx.edge(u8, &.{ 0, 1, 2, 6 }));
            }
            _ = putRecord(ctx, tex, tex_pos, ctx.edge(u8, &.{ 3, 4, 5, 7, 8, 200 }));
            tex_len = @intCast(tex_pos + ctx.range(usize, 1, 40));
        },
        else => {
            for (0..ctx.range(usize, 1, 12)) |_| {
                tex_pos = putRecord(ctx, tex, tex_pos, ctx.edge(u8, &.{ 0, 1, 2, 6 }));
            }
            _ = putRecord(ctx, tex, tex_pos, 7);
            tex_len = @intCast(tex_pos);
        },
    }
    room.header.textureEffectsSectionLen = tex_len;
    room.textureEffectsSection = @bitCast(psx.addr(tex));

    // Section D: zero-filled, so every short read past an item is 0 (the end
    // of its chunk list) unless a chunk header was written there.
    const sec = ctx.allocBytes(8192, 16);
    @memset(sec[0..8192], 0);
    const sd_state = ctx.range(usize, 0, 2);
    var sd_len: u32 = 0;
    if (sd_state == 0) {
        sd_len = ctx.edge(u32, &.{ 0, 0x8000_0000, 0xFFFF_FFFF });
    } else {
        var p: usize = 0;
        for (0..ctx.range(usize, 1, 8)) |_| {
            const count: usize = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5, 6, 7, 8 });
            sec[p] = @intCast(count);
            sec[p + 2] = if (ctx.chance(2)) 0 else 1; // active
            p += if ((count & 3) != 0) count + 8 - (count & 3) else count + 4;
            for (0..ctx.range(usize, 0, 3)) |_| {
                if (ctx.chance(3)) {
                    // a chunk whose short[1] is 11: skipped as 4 bytes
                    sec[p + 2] = 11;
                    p += 4;
                } else {
                    const size = ctx.edge(u8, &.{ 4, 6, 8, 12 });
                    sec[p] = size;
                    p += size;
                }
            }
            p += 4; // the terminator short pair (zero), then the 4 bytes the C skips
        }
        const total: u32 = @intCast(p);
        sd_len = if (sd_state == 2) total else total - ctx.range(u32, 1, total - 1);
    }
    room.header.sectionDLen = sd_len;
    room.sectionD = @bitCast(psx.addr(sec));

    class_index = tex_state * 3 + sd_state;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return class_index;
}
