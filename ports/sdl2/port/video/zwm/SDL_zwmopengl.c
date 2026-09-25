/* SPDX-License-Identifier: Zlib */
/* Copyright (C) 2026 Rigby Foundation */
/* SDL_GL_* on zwm through the sic GL context ABI (GL/sic_gl.h): one
 * offscreen context per window, rendered by whichever library the program
 * linked -- libzgl (OpenGL 2.1 on the host GPU through virgl) or libTinyGL
 * (software). Both draw 0x00RRGGBB frames, which is what zwm displays, into
 * a buffer the server maps (zwm_window_surface), so SDL_GL_SwapWindow only
 * tells it the frame changed.
 * When zwm composites on the GPU and the context has a host buffer (zgl),
 * nothing is read back at all: the window's pixels are that buffer, and a
 * swap is "flush, damage". */
#include "../../SDL_internal.h"

#if defined(SDL_VIDEO_DRIVER_ZWM) && defined(SDL_VIDEO_OPENGL)

#include "../SDL_sysvideo.h"
#include "SDL_zwmvideo.h"
#include <GL/sic_gl.h>

typedef struct {
    sic_gl_context *gl;
    SDL_Window *window;
    zwm_surface *fb;            /* shared with the server; the GL's swap target (not on the GPU path) */
    int w, h;
    int gpu;                    /* the server samples sic_gl_buffer() */
} ZwmGLContext;

static void retarget(ZwmGLContext *ctx)
{
    sic_gl_set_target(ctx->gl, ctx->fb->pix, ctx->fb->w);
}

/* The GPU path: tell the server which buffer the window is (again after a resize). */
static int attach_gpu(_THIS, SDL_Window *window, ZwmGLContext *ctx)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = window->driverdata;
    uint32_t res = sic_gl_buffer(ctx->gl);
    if (!res) return -1;
    return zwm_attach_gpu(v->conn, w->win, res, ctx->w, ctx->h);
}

static ZwmGLContext *current;
static int swap_interval = 1;       /* on the GPU path: wait for the compositor (0 = as fast as it goes) */

int ZWM_GL_LoadLibrary(_THIS, const char *path) { (void)_this; (void)path; return 0; }
void ZWM_GL_UnloadLibrary(_THIS) { (void)_this; }
void *ZWM_GL_GetProcAddress(_THIS, const char *proc) { (void)_this; return sic_gl_proc(proc); }

SDL_GLContext ZWM_GL_CreateContext(_THIS, SDL_Window *window)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = window->driverdata;
    ZwmGLContext *ctx = SDL_calloc(1, sizeof(*ctx));
    if (!ctx) { SDL_OutOfMemory(); return NULL; }
    SDL_GetWindowSizeInPixels(window, &ctx->w, &ctx->h);
    ctx->gl = sic_gl_create(ctx->w, ctx->h);
    if (!ctx->gl) { SDL_free(ctx); SDL_SetError("no OpenGL context (no GPU?)"); return NULL; }
    ctx->window = window;
    if (zwm_gpu_composited() && !SDL_getenv("SDL_ZWM_READBACK") && attach_gpu(_this, window, ctx) == 0) {
        ctx->gpu = 1;
        current = ctx;
        return ctx;
    }
    ctx->fb = zwm_window_surface(v->conn, w->win, ctx->w, ctx->h);
    if (!ctx->fb) { sic_gl_destroy(ctx->gl); SDL_free(ctx); SDL_OutOfMemory(); return NULL; }
    retarget(ctx);
    current = ctx;
    return ctx;
}

int ZWM_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context)
{
    ZwmGLContext *ctx = context;
    (void)_this;
    if (!ctx) return 0;
    ctx->window = window;
    sic_gl_make_current(ctx->gl);
    current = ctx;
    return 0;
}

int ZWM_GL_SetSwapInterval(_THIS, int interval) { (void)_this; swap_interval = interval; return 0; }
int ZWM_GL_GetSwapInterval(_THIS) { (void)_this; return swap_interval; }

int ZWM_GL_SwapWindow(_THIS, SDL_Window *window)
{
    SDL_ZwmVideoData *v = _this->driverdata;
    SDL_ZwmWindowData *w = window->driverdata;
    ZwmGLContext *ctx = current;
    const uint32_t *px;
    int cw, ch, stride;
    if (!ctx || !w) return SDL_SetError("no GL context");
    SDL_GetWindowSizeInPixels(window, &cw, &ch);
    if (cw != ctx->w || ch != ctx->h) {
        /* the window changed size: new buffers, the app redraws next frame */
        if (ctx->gpu) {
            if (sic_gl_resize(ctx->gl, cw, ch) == 0) { ctx->w = cw; ctx->h = ch; attach_gpu(_this, window, ctx); }
        } else if (sic_gl_resize(ctx->gl, cw, ch) == 0 && zwm_surface_resize(ctx->fb, cw, ch) == 0) {
            ctx->w = cw; ctx->h = ch;
            retarget(ctx);
        }
        return 0;
    }
    if (ctx->gpu) {
        /* the server's pace: this frame is on screen before the next is drawn
         * (bounded, so a server that stops drawing us, e.g. minimized, can't hang the app) */
        uint32_t since = zwm_frames(v->conn);
        sic_gl_flush(ctx->gl);
        zwm_damage(v->conn, w->win, 0, 0, ctx->w, ctx->h);
        if (swap_interval != 0) zwm_wait_frame(v->conn, since, 100);
        return 0;
    }
    px = sic_gl_swap(ctx->gl, &stride);
    if (px == ctx->fb->pix) zwm_flush(v->conn, w->win, ctx->fb);          /* already where the server looks */
    else if (px) zwm_blit(v->conn, w->win, 0, 0, ctx->w, ctx->h, px, stride);
    return 0;
}

void ZWM_GL_DeleteContext(_THIS, SDL_GLContext context)
{
    ZwmGLContext *ctx = context;
    (void)_this;
    if (!ctx) return;
    if (current == ctx) current = NULL;
    sic_gl_destroy(ctx->gl);
    if (ctx->fb) zwm_surface_free(ctx->fb);
    SDL_free(ctx);
}

#endif
