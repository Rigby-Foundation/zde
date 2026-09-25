/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* sdldemo: SDL2 on zwm. Bouncing squares through the software renderer;
 * click to add one, Escape or the close box to quit. */
#include <SDL2/SDL.h>
#include <stdio.h>

struct ball { float x, y, vx, vy; int size; Uint8 r, g, b; };

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("SDL2 on sic", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 480, 320, SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    printf("sdldemo: video driver %s, renderer software\n", SDL_GetCurrentVideoDriver());

    struct ball balls[64];
    int n = 0;
    srand(SDL_GetTicks());
    for (int i = 0; i < 4; i++) {
        balls[n++] = (struct ball){ 20.0f * (i + 1), 30.0f * (i + 1), 2.0f + i, 1.5f + i, 24 + 8 * i,
                                    (Uint8)(80 + 40 * i), (Uint8)(200 - 40 * i), (Uint8)(120 + 30 * i) };
    }
    int running = 1;
    Uint32 frames = 0, t0 = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (e.type == SDL_MOUSEBUTTONDOWN && n < 64)
                balls[n++] = (struct ball){ (float)e.button.x, (float)e.button.y, (float)(rand() % 7 - 3), (float)(rand() % 7 - 3), 16 + rand() % 24,
                                            (Uint8)rand(), (Uint8)rand(), (Uint8)rand() };
        }
        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        SDL_SetRenderDrawColor(ren, 24, 24, 32, 255);
        SDL_RenderClear(ren);
        for (int i = 0; i < n; i++) {
            struct ball *b = &balls[i];
            b->x += b->vx; b->y += b->vy;
            if (b->x < 0 || b->x + b->size > w) { b->vx = -b->vx; b->x += b->vx; }
            if (b->y < 0 || b->y + b->size > h) { b->vy = -b->vy; b->y += b->vy; }
            SDL_Rect r = { (int)b->x, (int)b->y, b->size, b->size };
            SDL_SetRenderDrawColor(ren, b->r, b->g, b->b, 255);
            SDL_RenderFillRect(ren, &r);
        }
        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
        SDL_RenderDrawLine(ren, 0, h - 1, w - 1, h - 1);
        SDL_RenderPresent(ren);
        frames++;
        SDL_Delay(16);
    }
    printf("sdldemo: %u frames in %u ms\n", frames, SDL_GetTicks() - t0);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
