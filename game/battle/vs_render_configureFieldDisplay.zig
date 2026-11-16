//! Screen geometry and the display and draw environments for a frame buffer (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_render_configureFieldDisplay (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the geometry and both frame buffers' display and draw environments for a width and height, with their background colour, then puts the current frame's environments.
pub fn vs_render_configureFieldDisplay(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int, arg4: c_int, arg5: c_int) callconv(.c) void {
    const temp_s3: c_uint = psx.conv(c_uint, arg1 -% 16);
    const half: c_int = psx.conv(c_int, temp_s3 +% (temp_s3 >> 31)) >> 1;
    const tail: c_int = psx.conv(c_int, temp_s3);

    _ = c.SetGeomOffset(@divTrunc(arg0, 2), half +% 0x10);
    _ = c.SetGeomScreen(arg2);

    const draw = v.g.vs_main_drawEnv();
    const disp = v.g.vs_main_dispEnv();

    _ = c.SetDefDrawEnv(psx.cptr(c.DRAWENV, &draw[0]), 0, 0, arg0, tail);
    _ = c.SetDefDispEnv(psx.cptr(c.DISPENV, &disp[0]), arg0, 0, arg0, tail);
    _ = c.SetDefDrawEnv(psx.cptr(c.DRAWENV, &draw[1]), arg0, 0, arg0, tail);
    _ = c.SetDefDispEnv(psx.cptr(c.DISPENV, &disp[1]), 0, 0, arg0, tail);

    // setRECT(&disp[n].screen, 0, 8, 256, 224), expanded as the PC object has it.
    disp[0].screen.x = 0;
    disp[0].screen.y = 8;
    disp[0].screen.w = 256;
    disp[0].screen.h = 224;
    disp[1].screen.x = 0;
    disp[1].screen.y = 8;
    disp[1].screen.w = 256;
    disp[1].screen.h = 224;

    v.g.D_8005DFD4().* = 0;
    v.g.D_8005DFD8().* = 0;
    v.g.vs_render_viewportWidth().* = psx.conv(i16, arg0);
    v.g.vs_render_viewportHeight().* = psx.conv(u16, temp_s3);

    draw[1].dtd = 1;
    draw[0].dtd = 1;
    draw[1].dfe = 0;
    draw[0].dfe = 0;
    draw[1].isbg = 0;
    draw[0].isbg = 0;

    // setRGB0 on both draw environments.
    draw[0].r0 = psx.conv(u8, arg3);
    draw[0].g0 = psx.conv(u8, arg4);
    draw[0].b0 = psx.conv(u8, arg5);
    draw[1].r0 = psx.conv(u8, arg3);
    draw[1].g0 = psx.conv(u8, arg4);
    draw[1].b0 = psx.conv(u8, arg5);

    _ = c.PutDispEnv(psx.cptr(c.DISPENV, psx.at(disp, v.g.vs_main_frameBuf().*)));
    _ = c.PutDrawEnv(psx.cptr(c.DRAWENV, psx.at(draw, v.g.vs_main_frameBuf().*)));
}
