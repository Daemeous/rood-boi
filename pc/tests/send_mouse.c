/* Test helper: drive the pointer over the rood-boi window (x, y in window
 * pixels) with the XTest extension. SDL reads the pointer through XInput2,
 * which events sent with XSendEvent never reach, so this moves the server's
 * real pointer (on a private Xvfb display, not the desktop's).
 *   send_mouse x y                      move
 *   send_mouse x y click [shift|ctrl]   press and release button 1
 *   send_mouse x y down|up BUTTON       press or release one button
 *   send_mouse rel DX DY                relative motion (the free camera)
 *   send_mouse button down|up BUTTON    press or release where the pointer is
 * Build: gcc pc/tests/send_mouse.c -lX11 -l:libXtst.so.6 -o pc/build/boot/send_mouse */
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <string.h>
#include <unistd.h>
/* libXtst, declared here because only its runtime library is installed. */
extern int XTestFakeMotionEvent(Display*, int, int, int, unsigned long);
extern int XTestFakeRelativeMotionEvent(Display*, int, int, unsigned long);
extern int XTestFakeButtonEvent(Display*, unsigned int, Bool, unsigned long);
extern int XTestFakeKeyEvent(Display*, unsigned int, Bool, unsigned long);
static Window find(Display* d, Window root)
{
    Window rr, parent, *children, found = 0;
    unsigned count;
    if (!XQueryTree(d, root, &rr, &parent, &children, &count))
        return 0;
    for (unsigned i = 0; i < count && !found; i++) {
        char* name = 0;
        if (XFetchName(d, children[i], &name) && name) {
            if (strstr(name, "rood-boi"))
                found = children[i];
            XFree(name);
        }
    }
    XFree(children);
    return found;
}
int main(int argc, char** argv)
{
    if (argc < 3)
        return 1;
    Display* d = XOpenDisplay(0);
    if (!d)
        return 1;
    Window root = DefaultRootWindow(d), target = find(d, root);
    if (!target)
        return 2;
    if (!strcmp(argv[1], "button") && argc > 3) {
        XTestFakeButtonEvent(d, (unsigned)atoi(argv[3]), !strcmp(argv[2], "down"), CurrentTime);
        XSync(d, False);
        XCloseDisplay(d);
        return 0;
    }
    if (!strcmp(argv[1], "rel") && argc > 3) {
        XTestFakeRelativeMotionEvent(d, atoi(argv[2]), atoi(argv[3]), CurrentTime);
        XSync(d, False);
        XCloseDisplay(d);
        return 0;
    }
    int x, y;
    Window child;
    XTranslateCoordinates(d, target, root, atoi(argv[1]), atoi(argv[2]), &x, &y, &child);
    XTestFakeMotionEvent(d, -1, x, y, CurrentTime);
    XSync(d, False);
    if (argc > 4 && (!strcmp(argv[3], "down") || !strcmp(argv[3], "up"))) {
        usleep(20000);
        XTestFakeButtonEvent(d, (unsigned)atoi(argv[4]), !strcmp(argv[3], "down"), CurrentTime);
        XSync(d, False);
    }
    if (argc > 3 && !strcmp(argv[3], "click")) {
        KeySym modifier = argc > 4 ? (!strcmp(argv[4], "shift") ? XK_Shift_L : !strcmp(argv[4], "ctrl") ? XK_Control_L : 0) : 0;
        unsigned keycode = modifier ? XKeysymToKeycode(d, modifier) : 0;
        if (keycode)
            XTestFakeKeyEvent(d, keycode, True, CurrentTime);
        usleep(50000);
        XTestFakeButtonEvent(d, 1, True, CurrentTime);
        XSync(d, False);
        usleep(80000);
        XTestFakeButtonEvent(d, 1, False, CurrentTime);
        if (keycode)
            XTestFakeKeyEvent(d, keycode, False, CurrentTime);
        XSync(d, False);
    }
    XCloseDisplay(d);
    return 0;
}
