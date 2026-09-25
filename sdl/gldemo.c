/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* gldemo: OpenGL through SDL2 on sic. A lit, spinning cube and a
 * triangle, as fast as the GL goes (no frame cap: the printed frame count
 * is a benchmark). Escape quits. */
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <stdio.h>
#include <math.h>

static void cube(void)
{
    static float v[8][3] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} };
    static const int f[6][4] = { {0,1,2,3},{4,7,6,5},{0,4,5,1},{3,2,6,7},{0,3,7,4},{1,5,6,2} };
    static float n[6][3] = { {0,0,-1},{0,0,1},{0,-1,0},{0,1,0},{-1,0,0},{1,0,0} };
    static float c[6][3] = { {0.9f,0.3f,0.3f},{0.3f,0.9f,0.3f},{0.3f,0.3f,0.9f},{0.9f,0.9f,0.3f},{0.3f,0.9f,0.9f},{0.9f,0.3f,0.9f} };
    glBegin(GL_QUADS);
    for (int i = 0; i < 6; i++) {
        glNormal3fv(n[i]);
        glColor3fv(c[i]);
        for (int k = 0; k < 4; k++) glVertex3fv(v[f[i][k]]);
    }
    glEnd();
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("OpenGL on sic", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 480, 360, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }
    printf("gldemo: GL_VENDOR %s, GL_RENDERER %s, GL_VERSION %s\n", (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    float light_pos[4] = { 2.0f, 3.0f, 4.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glClearColor(0.10f, 0.10f, 0.14f, 1.0f);

    int running = 1;
    float angle = 0;
    Uint32 frames = 0, t0 = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }
        int w, h;
        SDL_GL_GetDrawableSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float aspect = (float)w / (float)h, near = 1.0f, far = 20.0f, fh = 0.6f;
        glFrustum(-fh * aspect, fh * aspect, -fh, fh, near, far);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPushMatrix();
        glTranslatef(-1.2f, 0.0f, -6.0f);
        glRotatef(angle, 1.0f, 1.0f, 0.0f);
        cube();
        glPopMatrix();

        glPushMatrix();
        glTranslatef(1.8f, 0.0f, -6.0f);
        glRotatef(-angle * 1.5f, 0.0f, 1.0f, 0.0f);
        glDisable(GL_LIGHTING);
        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.2f, 0.2f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glColor3f(0.2f, 1.0f, 0.2f); glVertex3f(1.0f, -1.0f, 0.0f);
        glColor3f(0.2f, 0.2f, 1.0f); glVertex3f(0.0f, 1.2f, 0.0f);
        glEnd();
        glEnable(GL_LIGHTING);
        glPopMatrix();

        SDL_GL_SwapWindow(win);
        angle += 2.0f;
        frames++;
    }
    printf("gldemo: %u frames in %u ms\n", frames, SDL_GetTicks() - t0);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
