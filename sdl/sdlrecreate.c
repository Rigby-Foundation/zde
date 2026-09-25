/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* sdlrecreate: what Ren'Py does when it switches renderers -- a GL window,
 * torn down, then a plain one; twice. Prints ok or the SDL error. */
#include <SDL2/SDL.h>
#include <stdio.h>

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    for (int round = 0; round < 2; round++) {
        SDL_Window *w = SDL_CreateWindow("gl", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 200, SDL_WINDOW_OPENGL);
        if (!w) { printf("sdlrecreate: GL window: %s\n", SDL_GetError()); return 1; }
        SDL_GLContext c = SDL_GL_CreateContext(w);
        if (!c) printf("sdlrecreate: no GL context (%s), going on\n", SDL_GetError());
        else { SDL_GL_SwapWindow(w); SDL_GL_DeleteContext(c); }
        SDL_DestroyWindow(w);
        w = SDL_CreateWindow("sw", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 200, 0);
        if (!w) { printf("sdlrecreate: plain window after GL: %s\n", SDL_GetError()); return 1; }
        SDL_Surface *s = SDL_GetWindowSurface(w);
        if (!s) { printf("sdlrecreate: window surface: %s\n", SDL_GetError()); return 1; }
        SDL_FillRect(s, NULL, 0x336699); SDL_UpdateWindowSurface(w);
        SDL_DestroyWindow(w);
    }
    printf("sdlrecreate: ok\n");
    SDL_Quit();
    return 0;
}
