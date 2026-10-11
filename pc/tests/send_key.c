#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char** argv)
{
    Display* d = XOpenDisplay(0);
    if (!d)
        return 1;
    Window root = DefaultRootWindow(d), rr, parent, *children;
    unsigned count;
    if (!XQueryTree(d, root, &rr, &parent, &children, &count))
        return 1;
    Window target = 0;
    for (unsigned i = 0; i < count; i++) {
        char* name = 0;
        if (XFetchName(d, children[i], &name) && name) {
            if (strstr(name, "rood-boi"))
                target = children[i];
            XFree(name);
        }
    }
    XFree(children);
    if (!target)
        return 2;
    if (argc > 3 && !strcmp(argv[1], "--click")) {
        /* A left click at window coordinates (x, y): motion, press, release. */
        int x = atoi(argv[2]), y = atoi(argv[3]);
        XMotionEvent motion = { 0 };
        motion.type = MotionNotify;
        motion.window = target;
        motion.x = x, motion.y = y;
        XSendEvent(d, target, False, PointerMotionMask, (XEvent*)&motion);
        for (int down = 1; down >= 0; down--) {
            XButtonEvent button = { 0 };
            button.type = down ? ButtonPress : ButtonRelease;
            button.window = target;
            button.button = Button1;
            button.x = x, button.y = y;
            XSendEvent(d, target, False, down ? ButtonPressMask : ButtonReleaseMask, (XEvent*)&button);
            XSync(d, False);
            usleep(100000);
        }
        XCloseDisplay(d);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--close")) {
        /* What a window manager sends when the user closes the window. */
        XClientMessageEvent close = { 0 };
        close.type = ClientMessage;
        close.window = target;
        close.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
        close.format = 32;
        close.data.l[0] = (long)XInternAtom(d, "WM_DELETE_WINDOW", False);
        close.data.l[1] = CurrentTime;
        XSendEvent(d, target, False, NoEventMask, (XEvent*)&close);
        XSync(d, False);
        XCloseDisplay(d);
        return 0;
    }
    KeySym key = argc > 1 ? XStringToKeysym(argv[1]) : XK_Up;
    XKeyEvent event = { 0 };
    event.display = d;
    event.window = target;
    event.root = root;
    event.same_screen = True;
    event.keycode = XKeysymToKeycode(d, key);
    /* Optional second argument: "down" or "up" sends only that half. */
    const char* half = argc > 2 ? argv[2] : "";
    event.type = strcmp(half, "up") ? KeyPress : KeyRelease;
    XSendEvent(d, target, False, KeyPressMask, (XEvent*)&event);
    XFlush(d);
    if (*half) {
        XSync(d, False);
        XCloseDisplay(d);
        return 0;
    }
    usleep(100000);
    event.type = KeyRelease;
    XSendEvent(d, target, False, KeyReleaseMask, (XEvent*)&event);
    XFlush(d);
    XCloseDisplay(d);
    return 0;
}
