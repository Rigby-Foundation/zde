/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* blitbench: how fast do pixels get to zwm? Prints ms per full-window blit
 * through libzwm directly, through the SDL framebuffer, and through a GL
 * swap. */
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <zwm.h>
#include <stdio.h>

int main(void)
{
    zwm *c = zwm_connect();
    if (!c) { perror("zwm"); return 1; }
    struct zwm_m_geom g;
    int win = zwm_create(c, 480, 360, "blitbench", 0, &g);
    zwm_surface *s = zwm_surface_new(480, 360);
    Uint32 t0 = SDL_GetTicks();
    for (int i = 0; i < 20; i++) { zwm_fill(s, 0, 0, 480, 360, 0x102030 + i * 0x0a0a0a); zwm_flush(c, win, s); }
    printf("blitbench: libzwm blit %u ms each\n", (SDL_GetTicks() - t0) / 20);
    zwm_surface_free(s);
    s = zwm_window_surface(c, win, 480, 360);
    t0 = SDL_GetTicks();
    for (int i = 0; i < 20; i++) { zwm_fill(s, 0, 0, 480, 360, 0x302010 + i * 0x0a0a0a); zwm_flush(c, win, s); }
    printf("blitbench: libzwm shared flush %u ms each\n", (SDL_GetTicks() - t0) / 20);
    zwm_surface_free(s);
    zwm_destroy(c, win);
    zwm_disconnect(c);

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *w = SDL_CreateWindow("blitbench sdl", 0, 0, 480, 360, 0);
    SDL_Surface *surf = SDL_GetWindowSurface(w);
    t0 = SDL_GetTicks();
    for (int i = 0; i < 20; i++) { SDL_FillRect(surf, NULL, 0x203040 + i * 0x0a0a0a); SDL_UpdateWindowSurface(w); }
    printf("blitbench: SDL surface update %u ms each\n", (SDL_GetTicks() - t0) / 20);
    SDL_DestroyWindow(w);

    w = SDL_CreateWindow("blitbench gl", 0, 0, 480, 360, SDL_WINDOW_OPENGL);
    SDL_GLContext ctx = SDL_GL_CreateContext(w);
    t0 = SDL_GetTicks();
    for (int i = 0; i < 20; i++) { glClearColor(0.2f, 0.1f * (i % 5), 0.4f, 1); glClear(GL_COLOR_BUFFER_BIT); SDL_GL_SwapWindow(w); }
    printf("blitbench: GL clear+swap %u ms each\n", (SDL_GetTicks() - t0) / 20);
    t0 = SDL_GetTicks();
    for (int i = 0; i < 20; i++) { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
    printf("blitbench: GL clear only %u ms each\n", (SDL_GetTicks() - t0) / 20);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(w);
    SDL_Quit();
    return 0;
}
