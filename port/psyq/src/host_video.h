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
 * uv = n*(u,v) texels + tpage/clut for VRAM sampling, or uv==NULL = untextured. */
void hostVideoPoly(int n, const int *xy, const unsigned char *rgb,
                   const unsigned char *uv, int tpage, int clut);

unsigned long hostInputRead(void);

#endif /* PORT_HOST_VIDEO_H */
