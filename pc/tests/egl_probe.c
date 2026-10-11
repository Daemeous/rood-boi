/* Can the game render headless on a GPU? Opens SDL's offscreen video driver
 * with an OpenGL core context the way gl_present.c does (EGL on the device
 * that SDL_HINT_EGL_DEVICE names, else the first that initialises) and
 * prints the renderer. Exit status 0 when a context was created.
 *     egl_probe [DEVICE]      DEVICE: EGL device index (EGL_EXT_device_enumeration)
 * pc/tools/rood_rpc.py builds it (-m32, against the vendored SDL3) to choose
 * between ROOD_HEADLESS=egl and Xvfb. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

int main(int argc, char** argv)
{
    if (argc > 1)
        SDL_SetHint("SDL_HINT_EGL_DEVICE", argv[1]);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "offscreen video: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window* window = SDL_CreateWindow("egl_probe", 64, 64, SDL_WINDOW_OPENGL);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    if (!context || !SDL_GL_MakeCurrent(window, context)) {
        fprintf(stderr, "OpenGL context: %s\n", SDL_GetError());
        return 1;
    }
    const char* (*get_string)(unsigned) = (const char* (*)(unsigned))SDL_GL_GetProcAddress("glGetString");
    printf("%s | %s | %s\n", get_string(GL_VENDOR), get_string(GL_RENDERER), get_string(GL_VERSION));
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
