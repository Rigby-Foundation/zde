/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* The sic GL context ABI (GL/sic_gl.h) on TinyGL: a ZBuffer per context.
 * TinyGL renders into 0x00RRGGBB, which is what the ABI hands back, so a
 * swap is free. TinyGL has one global state: the last context made current
 * is the one that draws. */
#include <GL/gl.h>
#include <GL/sic_gl.h>
#include <zbuffer.h>
#include <stdlib.h>
#include <string.h>

struct sic_gl_context {
    ZBuffer *zb;
    uint32_t *target; int target_stride;    /* TinyGL can draw straight into it when its width fits */
};

static void *usable_target(sic_gl_context *c, int width)
{
    return c->target && c->target_stride == width && (width & 3) == 0 ? c->target : NULL;
}

static sic_gl_context *current;

sic_gl_context *sic_gl_create(int width, int height)
{
    sic_gl_context *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->zb = ZB_open(width, height, ZB_MODE_RGBA, NULL);
    if (!c->zb) { free(c); return NULL; }
    sic_gl_make_current(c);
    glViewport(0, 0, width, height);
    return c;
}

void sic_gl_make_current(sic_gl_context *c)
{
    if (current == c) return;
    if (current) glClose();
    current = c;
    if (c) glInit(c->zb);
}

int sic_gl_resize(sic_gl_context *c, int width, int height)
{
    if (width == c->zb->xsize && height == c->zb->ysize) return 0;
    ZB_resize(c->zb, usable_target(c, width), width, height);      /* new buffers, same state */
    return 0;
}

/* Software: no host buffer to share, no display to take. */
uint32_t sic_gl_buffer(sic_gl_context *c) { (void)c; return 0; }
int sic_gl_flush(sic_gl_context *c) { (void)c; return 0; }
int sic_gl_scanout(sic_gl_context *c) { (void)c; return -1; }
int sic_gl_present(sic_gl_context *c, int x, int y, int w, int h) { (void)c; (void)x; (void)y; (void)w; (void)h; return -1; }

void sic_gl_set_target(sic_gl_context *c, uint32_t *pix, int stride)
{
    c->target = pix; c->target_stride = stride;
    ZB_resize(c->zb, usable_target(c, c->zb->xsize), c->zb->xsize, c->zb->ysize);
}

const uint32_t *sic_gl_swap(sic_gl_context *c, int *stride)
{
    if (stride) *stride = c->zb->linesize / 4;
    return (const uint32_t *)c->zb->pbuf;
}

void sic_gl_destroy(sic_gl_context *c)
{
    if (!c) return;
    if (current == c) { glClose(); current = NULL; }
    ZB_close(c->zb);
    free(c);
}

static const struct { const char *name; void *fn; } gl_functions[] = {
    { "glAreTexturesResident", (void *)glAreTexturesResident },
    { "glArrayElement", (void *)glArrayElement },
    { "glBegin", (void *)glBegin },
    { "glBindBuffer", (void *)glBindBuffer },
    { "glBindBufferAsArray", (void *)glBindBufferAsArray },
    { "glBindTexture", (void *)glBindTexture },
    { "glBlendEquation", (void *)glBlendEquation },
    { "glBlendFunc", (void *)glBlendFunc },
    { "glBufferData", (void *)glBufferData },
    { "glCallList", (void *)glCallList },
    { "glCallLists", (void *)glCallLists },
    { "glClear", (void *)glClear },
    { "glClearColor", (void *)glClearColor },
    { "glClearDepth", (void *)glClearDepth },
    { "glClose", (void *)glClose },
    { "glColorMaterial", (void *)glColorMaterial },
    { "glColorPointer", (void *)glColorPointer },
    { "glCopyTexImage2D", (void *)glCopyTexImage2D },
    { "glCullFace", (void *)glCullFace },
    { "glDebug", (void *)glDebug },
    { "glDeleteBuffers", (void *)glDeleteBuffers },
    { "glDeleteList", (void *)glDeleteList },
    { "glDeleteLists", (void *)glDeleteLists },
    { "glDeleteTextures", (void *)glDeleteTextures },
    { "glDepthMask", (void *)glDepthMask },
    { "glDisable", (void *)glDisable },
    { "glDisableClientState", (void *)glDisableClientState },
    { "glDrawArrays", (void *)glDrawArrays },
    { "glDrawBuffer", (void *)glDrawBuffer },
    { "glDrawPixels", (void *)glDrawPixels },
    { "glDrawText", (void *)glDrawText },
    { "glEdgeFlag", (void *)glEdgeFlag },
    { "glEnable", (void *)glEnable },
    { "glEnableClientState", (void *)glEnableClientState },
    { "glEnd", (void *)glEnd },
    { "glEndList", (void *)glEndList },
    { "glFeedbackBuffer", (void *)glFeedbackBuffer },
    { "glFinish", (void *)glFinish },
    { "glFlush", (void *)glFlush },
    { "glFrontFace", (void *)glFrontFace },
    { "glFrustum", (void *)glFrustum },
    { "glGenBuffers", (void *)glGenBuffers },
    { "glGenLists", (void *)glGenLists },
    { "glGenTextures", (void *)glGenTextures },
    { "glGetError", (void *)glGetError },
    { "glGetFloatv", (void *)glGetFloatv },
    { "glGetIntegerv", (void *)glGetIntegerv },
    { "glGetString", (void *)glGetString },
    { "glGetTexturePixmap", (void *)glGetTexturePixmap },
    { "glHint", (void *)glHint },
    { "glInit", (void *)glInit },
    { "glInitNames", (void *)glInitNames },
    { "glIsBuffer", (void *)glIsBuffer },
    { "glIsList", (void *)glIsList },
    { "glIsTexture", (void *)glIsTexture },
    { "glLightModelfv", (void *)glLightModelfv },
    { "glLightModeli", (void *)glLightModeli },
    { "glLightf", (void *)glLightf },
    { "glLightfv", (void *)glLightfv },
    { "glListBase", (void *)glListBase },
    { "glLoadIdentity", (void *)glLoadIdentity },
    { "glLoadMatrixf", (void *)glLoadMatrixf },
    { "glLoadName", (void *)glLoadName },
    { "glMapBuffer", (void *)glMapBuffer },
    { "glMaterialf", (void *)glMaterialf },
    { "glMaterialfv", (void *)glMaterialfv },
    { "glMatrixMode", (void *)glMatrixMode },
    { "glMultMatrixf", (void *)glMultMatrixf },
    { "glNewList", (void *)glNewList },
    { "glNormalPointer", (void *)glNormalPointer },
    { "glPassThrough", (void *)glPassThrough },
    { "glPixelZoom", (void *)glPixelZoom },
    { "glPlotPixel", (void *)glPlotPixel },
    { "glPointSize", (void *)glPointSize },
    { "glPolygonMode", (void *)glPolygonMode },
    { "glPolygonOffset", (void *)glPolygonOffset },
    { "glPolygonStipple", (void *)glPolygonStipple },
    { "glPopMatrix", (void *)glPopMatrix },
    { "glPopName", (void *)glPopName },
    { "glPostProcess", (void *)glPostProcess },
    { "glPushMatrix", (void *)glPushMatrix },
    { "glPushName", (void *)glPushName },
    { "glRasterPos2f", (void *)glRasterPos2f },
    { "glRasterPos2fv", (void *)glRasterPos2fv },
    { "glRasterPos3f", (void *)glRasterPos3f },
    { "glRasterPos3fv", (void *)glRasterPos3fv },
    { "glRasterPos4f", (void *)glRasterPos4f },
    { "glRasterPos4fv", (void *)glRasterPos4fv },
    { "glReadBuffer", (void *)glReadBuffer },
    { "glReadPixels", (void *)glReadPixels },
    { "glRectf", (void *)glRectf },
    { "glRenderMode", (void *)glRenderMode },
    { "glRotatef", (void *)glRotatef },
    { "glScalef", (void *)glScalef },
    { "glSelectBuffer", (void *)glSelectBuffer },
    { "glSetEnableSpecular", (void *)glSetEnableSpecular },
    { "glShadeModel", (void *)glShadeModel },
    { "glTexCoordPointer", (void *)glTexCoordPointer },
    { "glTexEnvi", (void *)glTexEnvi },
    { "glTexImage1D", (void *)glTexImage1D },
    { "glTexImage2D", (void *)glTexImage2D },
    { "glTexParameteri", (void *)glTexParameteri },
    { "glTextSize", (void *)glTextSize },
    { "glTranslatef", (void *)glTranslatef },
    { "glVertexPointer", (void *)glVertexPointer },
    { "glViewport", (void *)glViewport },
};

void *sic_gl_proc(const char *name)
{
    for (size_t i = 0; i < sizeof gl_functions / sizeof gl_functions[0]; i++)
        if (strcmp(gl_functions[i].name, name) == 0) return gl_functions[i].fn;
    return NULL;
}
