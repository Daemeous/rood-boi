//! Applies a battle action's own stat cost, then each recorded stat delta, to the
//! actors' stat rows (src/BATTLE/BATTLE.PRG/74088.c).
const psx = @import("common").psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: func_800DC888 (src/BATTLE/BATTLE.PRG/74088.c)
/// Applies the action's cost, then each stat delta in the application record, to the stats rows.
pub fn func_800DC888(stats: ?*v.actionStatSnapshot) callconv(.c) void {
    const app: *v.actionStatApplication = @ptrCast(psx.get(v.g.D_800F5900().*).?);

    if (app.first.skip == 0) {
        // &vs_main_actions[action]: the C does not check the index against 256.
        const action: *align(1) v.actionCostView = @ptrCast(psx.at(v.g.actions(), app.action));
        const base0: usize = if (stats) |s| @intFromPtr(s) else 0;
        const row: ?*v.actionStatSnapshot = @ptrFromInt(base0 +% @as(usize, app.first.actor) *% @sizeOf(v.actionStatSnapshot));
        const amount: c_int = psx.byteAt(u8, action, 3).*;

        switch ((action.flags >> 17) & 7) {
            1 => c.func_800DC30C(&row.?.mp, &row.?.maxMP, amount, 1),
            3 => c.func_800DC30C(&row.?.hp, &row.?.maxHP, amount, 1),
            6 => c.func_800DC30C(&row.?.value10, &row.?.limit10, amount, 1),
            else => {},
        }
    }

    var i: c_int = -1;
    while (i < @as(c_int, app.count)) : (i +%= 1) {
        const entry: *v.actionStatDelta = if (i < 0) &app.first else psx.at(&app.entries, i);
        if (entry.skip == 0) {
            const base0: usize = if (stats) |s| @intFromPtr(s) else 0;
            const row: ?*v.actionStatSnapshot = @ptrFromInt(base0 +% @as(usize, entry.actor) *% @sizeOf(v.actionStatSnapshot));
            const r = row.?;
            c.func_800DC30C(&r.hp, &r.maxHP, @as(c_int, entry.hp), psx.conv(c_int, entry.modes & 3));
            c.func_800DC30C(&r.mp, &r.maxMP, @as(c_int, entry.mp), psx.conv(c_int, (entry.modes >> 2) & 3));
            c.func_800DC30C(&r.value10, &r.limit10, @as(c_int, entry.value10), psx.conv(c_int, (entry.modes >> 18) & 3));
            c.func_800DC30C(&r.value14, &r.limit14, @as(c_int, entry.value14), psx.conv(c_int, (entry.modes >> 22) & 3));
            r.status |= entry.addStatus;
            r.status &= ~entry.removeStatus;
        }
    }
}
