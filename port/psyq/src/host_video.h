/* host_video.h -- internal host window/GL surface shared by the PsyQ shims. */
#ifndef PORT_HOST_VIDEO_H
#define PORT_HOST_VIDEO_H

int  hostVideoInit(int w, int h);
void hostVideoShutdown(void);

int  hostVideoPump(void);        /* process SDL events; 0 => quit requested */
void hostVideoPresent(void);     /* swap buffers + cap to refresh (NO clear) */
void hostVideoSetRefresh(int hz);

/* Frame drawing (painter's order; caller sorts by OT-z). */
void hostVideoClear(unsigned char r, unsigned char g, unsigned char b); /* begin frame */
void hostVideoSetClear(unsigned char r, unsigned char g, unsigned char b);
/* Draw a flat/gouraud polygon, optionally textured. n = 3 or 4.
 * xy = n*(x,y) screen coords; rgb = n*(r,g,b) (flat polys repeat the colour).
 * uv = n*(u,v) texels + tpage/clut for VRAM sampling, or uv==NULL = untextured.
 * blend: 0 = opaque; else 1 + PSX ABR semi-transparency mode (0..3) =>
 *   1: 0.5*B+0.5*F   2: B+F   3: B-F   4: B+0.25*F  (B=framebuffer, F=prim). */
void hostVideoPoly(int n, const int *xy, const unsigned char *rgb,
                   const unsigned char *uv, int tpage, int clut, int blend);

unsigned long hostInputRead(void);

#endif /* PORT_HOST_VIDEO_H */
