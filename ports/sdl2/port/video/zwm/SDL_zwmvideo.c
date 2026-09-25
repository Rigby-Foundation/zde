/* SPDX-License-Identifier: Zlib */
/* Copyright (C) 2026 Rigby Foundation */
/* SDL2 video driver for zwm, the sic window server. Windows are zwm
 * windows; the SDL window framebuffer is blitted to them; input comes
 * back as zwm events and is turned into SDL's. */
#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_ZWM

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_windowevents_c.h"
#include "../../events/scancodes_windows.h"
#include "SDL_zwmvideo.h"

static void ZWM_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_ZwmVideoData *data = device->driverdata;
    if (data) {
        if (data->conn) zwm_disconnect(data->conn);
        SDL_free(data);
    }
    SDL_free(device);
}

static int ZWM_VideoInit(_THIS)
{
    SDL_ZwmVideoData *data = _this->driverdata;
    SDL_DisplayMode mode;
    /* the server tells the screen size when a window is made: ask with a
     * throwaway one */
    struct zwm_m_geom g;
    int probe = zwm_create(data->conn, 1, 1, "", ZWM_UNDECORATED, &g);
    if (probe < 0) return SDL_SetError("zwm: cannot create a window");
    zwm_destroy(data->conn, probe);
    data->screen_w = g.screen_w;
    data->screen_h = g.screen_h;

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB888;
    mode.w = g.screen_w;
    mode.h = g.screen_h;
    mode.refresh_rate = 60;
    if (SDL_AddBasicVideoDisplay(&mode) < 0) return -1;
    SDL_AddDisplayMode(&_this->displays[0], &mode);
    return 0;
}

static void ZWM_VideoQuit(_THIS) { (void)_this; }

static SDL_VideoDevice *ZWM_CreateDevice(void)
{
    SDL_VideoDevice *device;
    SDL_ZwmVideoData *data;
    zwm *conn = zwm_connect();
    if (!conn) return NULL;                     /* no server: the dummy driver is next in line */

    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    data = (SDL_ZwmVideoData *)SDL_calloc(1, sizeof(SDL_ZwmVideoData));
    if (!device || !data) {
        SDL_free(device); SDL_free(data); zwm_disconnect(conn);
        SDL_OutOfMemory();
        return NULL;
    }
    data->conn = conn;
    device->driverdata = data;

    device->VideoInit = ZWM_VideoInit;
    device->VideoQuit = ZWM_VideoQuit;
    device->PumpEvents = ZWM_PumpEvents;
    device->CreateSDLWindow = ZWM_CreateWindow;
    device->DestroyWindow = ZWM_DestroyWindow;
    device->SetWindowTitle = ZWM_SetWindowTitle;
    device->RaiseWindow = ZWM_RaiseWindow;
    device->CreateWindowFramebuffer = ZWM_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = ZWM_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = ZWM_DestroyWindowFramebuffer;
#ifdef SDL_VIDEO_OPENGL
    extern int ZWM_GL_LoadLibrary(_THIS, const char *path);
    extern void *ZWM_GL_GetProcAddress(_THIS, const char *proc);
    extern void ZWM_GL_UnloadLibrary(_THIS);
    extern SDL_GLContext ZWM_GL_CreateContext(_THIS, SDL_Window *window);
    extern int ZWM_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context);
    extern int ZWM_GL_SetSwapInterval(_THIS, int interval);
    extern int ZWM_GL_GetSwapInterval(_THIS);
    extern int ZWM_GL_SwapWindow(_THIS, SDL_Window *window);
    extern void ZWM_GL_DeleteContext(_THIS, SDL_GLContext context);
    device->GL_LoadLibrary = ZWM_GL_LoadLibrary;
    device->GL_GetProcAddress = ZWM_GL_GetProcAddress;
    device->GL_UnloadLibrary = ZWM_GL_UnloadLibrary;
    device->GL_CreateContext = ZWM_GL_CreateContext;
    device->GL_MakeCurrent = ZWM_GL_MakeCurrent;
    device->GL_SetSwapInterval = ZWM_GL_SetSwapInterval;
    device->GL_GetSwapInterval = ZWM_GL_GetSwapInterval;
    device->GL_SwapWindow = ZWM_GL_SwapWindow;
    device->GL_DeleteContext = ZWM_GL_DeleteContext;
#endif
    device->free = ZWM_DeleteDevice;
    return device;
}

VideoBootStrap ZWM_bootstrap = {
    "zwm", "sic zwm window server",
    ZWM_CreateDevice,
    NULL
};

/* ---- windows ------------------------------------------------------------ */

int ZWM_CreateWindow(_THIS, SDL_Window *window)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = SDL_calloc(1, sizeof(*w));
    struct zwm_m_geom g;
    Uint32 flags = 0;
    if (!w) return SDL_OutOfMemory();
    if (window->flags & SDL_WINDOW_BORDERLESS) flags |= ZWM_UNDECORATED;
    if (!(window->flags & SDL_WINDOW_RESIZABLE)) flags |= ZWM_FIXED;
    if (window->flags & SDL_WINDOW_FULLSCREEN) flags = ZWM_UNDECORATED | ZWM_FIXED;
    w->win = zwm_create(v->conn, window->w, window->h, window->title ? window->title : "SDL", flags, &g);
    if (w->win < 0) { SDL_free(w); return SDL_SetError("zwm: window creation failed"); }
    window->driverdata = w;
    window->x = g.x; window->y = g.y;
    if (g.w != window->w || g.h != window->h) {
        window->w = g.w; window->h = g.h;
        SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESIZED, g.w, g.h);
    }
    window->flags |= SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS;
    SDL_SetKeyboardFocus(window);
    return 0;
}

void ZWM_DestroyWindow(_THIS, SDL_Window *window)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = window->driverdata;
    if (!w) return;
    ZWM_DestroyWindowFramebuffer(_this, window);
    zwm_destroy(v->conn, w->win);
    SDL_free(w);
    window->driverdata = NULL;
}

void ZWM_SetWindowTitle(_THIS, SDL_Window *window)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = window->driverdata;
    if (w) zwm_set_title(v->conn, w->win, window->title ? window->title : "");
}

void ZWM_RaiseWindow(_THIS, SDL_Window *window)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = window->driverdata;
    if (w) zwm_raise(v->conn, w->win);
}

SDL_Window *ZWM_FindWindow(_THIS, int win)
{
    SDL_Window *window;
    for (window = _this->windows; window; window = window->next) {
        SDL_ZwmWindowData *w = window->driverdata;
        if (w && w->win == win) return window;
    }
    return NULL;
}

/* ---- the window framebuffer ------------------------------------------- */

int ZWM_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = window->driverdata;
    int cw, ch;
    ZWM_DestroyWindowFramebuffer(_this, window);
    SDL_GetWindowSizeInPixels(window, &cw, &ch);
    w->fb = zwm_window_surface(v->conn, w->win, cw, ch);   /* the server maps it: updates are damage only */
    if (!w->fb) return SDL_OutOfMemory();
    *format = SDL_PIXELFORMAT_RGB888;
    *pixels = w->fb->pix;
    *pitch = w->fb->w * 4;
    return 0;
}

int ZWM_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = window->driverdata;
    int i;
    if (!w || !w->fb) return SDL_SetError("no window framebuffer");
    for (i = 0; i < numrects; i++)
        zwm_flush_rect(v->conn, w->win, w->fb, rects[i].x, rects[i].y, rects[i].w, rects[i].h);
    return 0;
}

void ZWM_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    SDL_ZwmWindowData *w = window->driverdata;
    (void)_this;
    if (w && w->fb) { zwm_surface_free(w->fb); w->fb = NULL; }
}

/* ---- events ------------------------------------------------------------ */

static SDL_Scancode scancode_of(const struct zwm_m_key *k)
{
    Uint8 raw = (Uint8)(k->scancode & 0x7F);
    switch (k->sym) {                           /* the E0-prefixed ones the raw byte can't tell apart */
    case ZWM_KEY_UP: return SDL_SCANCODE_UP;       case ZWM_KEY_DOWN: return SDL_SCANCODE_DOWN;
    case ZWM_KEY_LEFT: return SDL_SCANCODE_LEFT;   case ZWM_KEY_RIGHT: return SDL_SCANCODE_RIGHT;
    case ZWM_KEY_HOME: return SDL_SCANCODE_HOME;   case ZWM_KEY_END: return SDL_SCANCODE_END;
    case ZWM_KEY_PGUP: return SDL_SCANCODE_PAGEUP; case ZWM_KEY_PGDN: return SDL_SCANCODE_PAGEDOWN;
    case ZWM_KEY_INSERT: return SDL_SCANCODE_INSERT; case ZWM_KEY_DELETE: return SDL_SCANCODE_DELETE;
    default: break;
    }
    if (raw < SDL_arraysize(windows_scancode_table)) return windows_scancode_table[raw];
    return SDL_SCANCODE_UNKNOWN;
}

void ZWM_PumpEvents(_THIS)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    zwm_event ev;
    int r;
    while ((r = zwm_next_event(v->conn, &ev, 0)) > 0) {
        SDL_Window *window = ZWM_FindWindow(_this, (int)ev.win);
        if (!window) continue;
        switch (ev.type) {
        case ZWM_S_KEY: {
            SDL_Scancode sc = scancode_of(&ev.key);
            if (sc != SDL_SCANCODE_UNKNOWN)
                SDL_SendKeyboardKey(ev.key.down ? SDL_PRESSED : SDL_RELEASED, sc);
            if (ev.key.down && ev.key.sym >= 0x20 && ev.key.sym < 0x7F && !(ev.key.mods & (ZWM_MOD_CTRL | ZWM_MOD_ALT))) {
                char text[2] = { (char)ev.key.sym, 0 };
                SDL_SendKeyboardText(text);
            }
            break;
        }
        case ZWM_S_MOUSE: {
            static Uint32 held;
            Uint32 b = ev.mouse.buttons;
            SDL_SetMouseFocus(window);
            SDL_SendMouseMotion(window, 0, 0, ev.mouse.x, ev.mouse.y);
            if (ev.mouse.kind != ZWM_MOUSE_MOVE) {
                Uint32 changed = held ^ b;
                if (changed & ZWM_BTN_LEFT)   SDL_SendMouseButton(window, 0, (b & ZWM_BTN_LEFT) ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_LEFT);
                if (changed & ZWM_BTN_RIGHT)  SDL_SendMouseButton(window, 0, (b & ZWM_BTN_RIGHT) ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_RIGHT);
                if (changed & ZWM_BTN_MIDDLE) SDL_SendMouseButton(window, 0, (b & ZWM_BTN_MIDDLE) ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_MIDDLE);
                held = b;
            }
            break;
        }
        case ZWM_S_RESIZE:
            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESIZED, ev.geom.w, ev.geom.h);
            break;
        case ZWM_S_FOCUS:
            if (ev.focus.focused) { SDL_SetKeyboardFocus(window); SDL_SendWindowEvent(window, SDL_WINDOWEVENT_FOCUS_GAINED, 0, 0); }
            else SDL_SendWindowEvent(window, SDL_WINDOWEVENT_FOCUS_LOST, 0, 0);
            break;
        case ZWM_S_CLOSE:
            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_CLOSE, 0, 0);
            break;
        default:
            break;
        }
    }
    if (r < 0) {                                /* the server is gone: tell every window */
        SDL_Window *window;
        for (window = _this->windows; window; window = window->next)
            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_CLOSE, 0, 0);
    }
}

#endif /* SDL_VIDEO_DRIVER_ZWM */
