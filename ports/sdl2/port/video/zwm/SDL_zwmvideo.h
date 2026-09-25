/* SPDX-License-Identifier: Zlib */
/* Copyright (C) 2026 Rigby Foundation */
#ifndef SDL_zwmvideo_h_
#define SDL_zwmvideo_h_
#include "../SDL_sysvideo.h"
#include <zwm.h>

typedef struct {
    zwm *conn;
    int screen_w, screen_h;
} SDL_ZwmVideoData;

typedef struct {
    int win;                    /* zwm window id */
    zwm_surface *fb;            /* the window framebuffer, shared with the server */            /* the window framebuffer, if the app uses one */
    void *gl;                   /* the software GL context, if any (SDL_zwmopengl.c) */
} SDL_ZwmWindowData;

extern int  ZWM_CreateWindow(_THIS, SDL_Window *window);
extern void ZWM_DestroyWindow(_THIS, SDL_Window *window);
extern void ZWM_SetWindowTitle(_THIS, SDL_Window *window);
extern void ZWM_RaiseWindow(_THIS, SDL_Window *window);
extern int  ZWM_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch);
extern int  ZWM_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects);
extern void ZWM_DestroyWindowFramebuffer(_THIS, SDL_Window *window);
extern void ZWM_PumpEvents(_THIS);
extern SDL_Window *ZWM_FindWindow(_THIS, int win);

#endif
