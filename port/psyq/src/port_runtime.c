/* port_runtime.c -- MR engine globals + libgpu/libetc host backend.
 *
 * Includes mr_all.h so the MR global types and the real PsyQ prototypes match.
 * The GPU/ETC functions are backed by the SDL/GL host (host_video.c); the
 * window appears at ResetGraph and VSync() is the frame heartbeat. Nothing is
 * rasterised yet (DrawOTag is a no-op) -- that is the GPU-raster milestone.
 */
#include "mr_all.h"
#include "host_video.h"
#include <stdlib.h>

/* ---- MR engine globals (originally defined in fastram.s) ---- */
MR_ULONG       MRFrame_index;
MR_VIEWPORT*   MRDefault_vp;
MR_ULONG*      MRDefault_vp_ot;
MR_LONG        MRFast_long;
MR_MAT         MRViewtrans;
MR_MAT*        MRViewtrans_ptr;
MR_MAT*        MRWorldtrans_ptr;
MR_MAT         MRLight_matrix;
MR_MAT*        MRLight_matrix_ptr;
MR_MAT         MRTemp_matrix;
MR_SVEC        MRTemp_svec;
MR_VIEWPORT*   MRVp_ptr;
MR_USHORT      MRVp_otz_shift;
MR_USHORT      MRVp_ot_size;
MR_USHORT      MRVp_disp_w;
MR_USHORT      MRVp_disp_h;
MR_ULONG       MRVp_fog_near_distance;
MR_ULONG       MRVp_fog_far_distance;
MR_ULONG*      MRVp_work_ot;

/* ---- data blobs ---- frogpsx_mwi (MWI index) and card_image (save TIM) are
 * now embedded from the real repo binaries in port_binaries.S. ---- */
MR_UBYTE       MRCd_error_pp[1];
MR_ULONG       MRCd_error_len = 0;
MR_SHORT       MRAcos_table[0x2001];  /* arccos LUT, input -0x1000..0x1000 */

/* ---- main RAM heap (PSX used a fixed RAM region; we malloc one) ----
 * MAIN.C does MRInitMem(__heapbase, __heapsize - 10k). __heapbase is a real
 * pointer and __heapsize its byte size. The PSX had ~2MB; we give generous
 * headroom. Allocated before main() via a constructor. */
MR_ULONG* __heapbase;
MR_ULONG  __heapsize;
__attribute__((constructor)) static void port_heap_init(void)
{
    __heapsize = 16u * 1024u * 1024u;          /* 16 MB */
    __heapbase = (MR_ULONG*)malloc(__heapsize);
}

/* ---- primitive recording state (shared by port_addprim / DrawOTag) ---- */
#define PORT_MAX_PRIMS 16384
static void *g_prims[PORT_MAX_PRIMS];
static long  g_prim_otz[PORT_MAX_PRIMS];
static int   g_prim_count;
int          port_last_prims;   /* last frame's prim count (read by host) */
static int   g_cur_tpage;       /* current texture page (set by DR_TPAGE) */
int          port_cnt_sprt, port_cnt_tile, port_cnt_tpage;  /* per-frame, host reads */
static void  port_draw_prim(void *p);

/* ---- libgpu (host-backed) ---- */
static int g_w = 320, g_h = 240;

int ResetGraph(int mode) { (void)mode; return hostVideoInit(g_w, g_h); }
int SetGraphDebug(int level) { (void)level; return 0; }
void SetDispMask(int mask) { (void)mask; }
int DrawSync(int mode) { (void)mode; return 0; }
void DrawOTag(u_long *ot)   /* draw recorded prims back-to-front, then reset */
{
    int i, j;
    (void)ot;
    /* insertion sort by otz descending (stable; lists are small per frame) */
    for (i = 1; i < g_prim_count; ++i) {
        void *pp = g_prims[i]; long oz = g_prim_otz[i];
        for (j = i - 1; j >= 0 && g_prim_otz[j] < oz; --j) {
            g_prims[j+1] = g_prims[j]; g_prim_otz[j+1] = g_prim_otz[j];
        }
        g_prims[j+1] = pp; g_prim_otz[j+1] = oz;
    }
    g_cur_tpage = 0;
    port_cnt_sprt = port_cnt_tile = port_cnt_tpage = 0;
    for (i = 0; i < g_prim_count; ++i) port_draw_prim(g_prims[i]);
    port_last_prims = g_prim_count;
    g_prim_count = 0;
}

int ClearImage(RECT *r, u_char rr, u_char g, u_char b) { (void)r; hostVideoSetClear(rr, g, b); return 0; }

/* ---- PSX VRAM (1024 x 512 x 16-bit) for textures/CLUTs; host reads it ---- */
unsigned short port_vram[1024*512];
int            port_vram_gen;      /* bumped on upload -> host cache invalidation */

int LoadImage(RECT *r, u_long *p)
{
    unsigned short *src = (unsigned short*)p;
    int row, col;
    if (!r) return 0;
    for (row = 0; row < r->h; ++row)
        for (col = 0; col < r->w; ++col) {
            int vx = r->x + col, vy = r->y + row;
            if (vx>=0 && vx<1024 && vy>=0 && vy<512)
                port_vram[vy*1024 + vx] = src[row*r->w + col];
        }
    port_vram_gen++;
    return 0;
}
int StoreImage(RECT *r, u_long *p) { (void)r; (void)p; return 0; }
int MoveImage(RECT *r, int x, int y) { (void)r; (void)x; (void)y; return 0; }

u_long *ClearOTag(u_long *ot, int n) { int i; for (i=0;i<n;i++) ot[i]=0xffffff; return ot; }
u_long *ClearOTagR(u_long *ot, int n) { int i; for (i=0;i<n;i++) ot[i]=0xffffff; return ot; }

/* ---- primitive recording (replaces the PSX 24-bit OT linked list) ---- */
void port_addprim(void *otentry, void *prim)
{
    long otz = 0;
    if (MRVp_work_ot) {
        otz = (u_long*)otentry - MRVp_work_ot;
        if (otz < 0 || otz >= (long)MRVp_ot_size) otz = 0;
    }
    if (g_prim_count < PORT_MAX_PRIMS) {
        g_prims[g_prim_count]   = prim;
        g_prim_otz[g_prim_count] = otz;
        g_prim_count++;
    }
}

/* Decode one PSX primitive into screen-space verts + colours (+ texture) and
 * draw it. uv==NULL => untextured; else pass tpage/clut for VRAM sampling. */
static void port_draw_prim(void *p)
{
    unsigned char code = ((unsigned char*)p)[7];   /* header byte: tag(4)+rgb(3)+code */
    int xy[8]; unsigned char rgb[12]; unsigned char uv[8];

    /* GPU state commands (0xe1 = texture page, etc.): update state, don't draw */
    if (code >= 0xe0) {
        if (code == 0xe1) { g_cur_tpage = ((u_long*)p)[1] & 0x1ff; port_cnt_tpage++; }  /* DR_TPAGE */
        return;
    }
    /* Sprites/tiles: 2D rects. SPRT is textured with the current tpage. */
    if ((code & 0xe0) == 0x60) {
        if ((code & 0xfc) == 0x64) {                 /* SPRT */
            SPRT *q = (SPRT*)p; port_cnt_sprt++;
            int x0=q->x0, y0=q->y0, w=q->w, h=q->h, u0=q->u0, v0=q->v0, i;
            xy[0]=x0;   xy[1]=y0;    xy[2]=x0+w; xy[3]=y0;
            xy[4]=x0;   xy[5]=y0+h;  xy[6]=x0+w; xy[7]=y0+h;
            uv[0]=u0;   uv[1]=v0;    uv[2]=u0+w; uv[3]=v0;
            uv[4]=u0;   uv[5]=v0+h;  uv[6]=u0+w; uv[7]=v0+h;
            for(i=0;i<4;i++){rgb[i*3]=q->r0;rgb[i*3+1]=q->g0;rgb[i*3+2]=q->b0;}
            hostVideoPoly(4,xy,rgb,uv,g_cur_tpage,q->clut);
        } else if ((code & 0xfc) == 0x60) {          /* TILE (flat rect) */
            TILE *q = (TILE*)p; port_cnt_tile++;
            int x0=q->x0, y0=q->y0, w=q->w, h=q->h, i;
            xy[0]=x0;   xy[1]=y0;    xy[2]=x0+w; xy[3]=y0;
            xy[4]=x0;   xy[5]=y0+h;  xy[6]=x0+w; xy[7]=y0+h;
            for(i=0;i<4;i++){rgb[i*3]=q->r0;rgb[i*3+1]=q->g0;rgb[i*3+2]=q->b0;}
            hostVideoPoly(4,xy,rgb,0,0,0);
        }
        return;
    }
    switch (code & 0xfc) {
    case 0x20: { POLY_F3 *q=(POLY_F3*)p; int i;              /* flat tri */
        xy[0]=q->x0;xy[1]=q->y0; xy[2]=q->x1;xy[3]=q->y1; xy[4]=q->x2;xy[5]=q->y2;
        for(i=0;i<3;i++){rgb[i*3]=q->r0;rgb[i*3+1]=q->g0;rgb[i*3+2]=q->b0;}
        hostVideoPoly(3,xy,rgb,0,0,0); } break;
    case 0x24: { POLY_FT3 *q=(POLY_FT3*)p; int i;            /* flat tex tri */
        xy[0]=q->x0;xy[1]=q->y0; xy[2]=q->x1;xy[3]=q->y1; xy[4]=q->x2;xy[5]=q->y2;
        uv[0]=q->u0;uv[1]=q->v0; uv[2]=q->u1;uv[3]=q->v1; uv[4]=q->u2;uv[5]=q->v2;
        for(i=0;i<3;i++){rgb[i*3]=q->r0;rgb[i*3+1]=q->g0;rgb[i*3+2]=q->b0;}
        hostVideoPoly(3,xy,rgb,uv,q->tpage,q->clut); } break;
    case 0x28: { POLY_F4 *q=(POLY_F4*)p; int i;              /* flat quad */
        xy[0]=q->x0;xy[1]=q->y0; xy[2]=q->x1;xy[3]=q->y1; xy[4]=q->x2;xy[5]=q->y2; xy[6]=q->x3;xy[7]=q->y3;
        for(i=0;i<4;i++){rgb[i*3]=q->r0;rgb[i*3+1]=q->g0;rgb[i*3+2]=q->b0;}
        hostVideoPoly(4,xy,rgb,0,0,0); } break;
    case 0x2c: { POLY_FT4 *q=(POLY_FT4*)p; int i;            /* flat tex quad */
        xy[0]=q->x0;xy[1]=q->y0; xy[2]=q->x1;xy[3]=q->y1; xy[4]=q->x2;xy[5]=q->y2; xy[6]=q->x3;xy[7]=q->y3;
        uv[0]=q->u0;uv[1]=q->v0; uv[2]=q->u1;uv[3]=q->v1; uv[4]=q->u2;uv[5]=q->v2; uv[6]=q->u3;uv[7]=q->v3;
        for(i=0;i<4;i++){rgb[i*3]=q->r0;rgb[i*3+1]=q->g0;rgb[i*3+2]=q->b0;}
        hostVideoPoly(4,xy,rgb,uv,q->tpage,q->clut); } break;
    case 0x30: { POLY_G3 *q=(POLY_G3*)p;                     /* gouraud tri */
        xy[0]=q->x0;xy[1]=q->y0; xy[2]=q->x1;xy[3]=q->y1; xy[4]=q->x2;xy[5]=q->y2;
        rgb[0]=q->r0;rgb[1]=q->g0;rgb[2]=q->b0; rgb[3]=q->r1;rgb[4]=q->g1;rgb[5]=q->b1; rgb[6]=q->r2;rgb[7]=q->g2;rgb[8]=q->b2;
        hostVideoPoly(3,xy,rgb,0,0,0); } break;
    case 0x34: { POLY_GT3 *q=(POLY_GT3*)p;                   /* gouraud tex tri */
        xy[0]=q->x0;xy[1]=q->y0; xy[2]=q->x1;xy[3]=q->y1; xy[4]=q->x2;xy[5]=q->y2;
        uv[0]=q->u0;uv[1]=q->v0; uv[2]=q->u1;uv[3]=q->v1; uv[4]=q->u2;uv[5]=q->v2;
        rgb[0]=q->r0;rgb[1]=q->g0;rgb[2]=q->b0; rgb[3]=q->r1;rgb[4]=q->g1;rgb[5]=q->b1; rgb[6]=q->r2;rgb[7]=q->g2;rgb[8]=q->b2;
        hostVideoPoly(3,xy,rgb,uv,q->tpage,q->clut); } break;
    case 0x38: { POLY_G4 *q=(POLY_G4*)p;                     /* gouraud quad */
        xy[0]=q->x0;xy[1]=q->y0; xy[2]=q->x1;xy[3]=q->y1; xy[4]=q->x2;xy[5]=q->y2; xy[6]=q->x3;xy[7]=q->y3;
        rgb[0]=q->r0;rgb[1]=q->g0;rgb[2]=q->b0; rgb[3]=q->r1;rgb[4]=q->g1;rgb[5]=q->b1;
        rgb[6]=q->r2;rgb[7]=q->g2;rgb[8]=q->b2; rgb[9]=q->r3;rgb[10]=q->g3;rgb[11]=q->b3;
        hostVideoPoly(4,xy,rgb,0,0,0); } break;
    case 0x3c: { POLY_GT4 *q=(POLY_GT4*)p;                   /* gouraud tex quad */
        xy[0]=q->x0;xy[1]=q->y0; xy[2]=q->x1;xy[3]=q->y1; xy[4]=q->x2;xy[5]=q->y2; xy[6]=q->x3;xy[7]=q->y3;
        uv[0]=q->u0;uv[1]=q->v0; uv[2]=q->u1;uv[3]=q->v1; uv[4]=q->u2;uv[5]=q->v2; uv[6]=q->u3;uv[7]=q->v3;
        rgb[0]=q->r0;rgb[1]=q->g0;rgb[2]=q->b0; rgb[3]=q->r1;rgb[4]=q->g1;rgb[5]=q->b1;
        rgb[6]=q->r2;rgb[7]=q->g2;rgb[8]=q->b2; rgb[9]=q->r3;rgb[10]=q->g3;rgb[11]=q->b3;
        hostVideoPoly(4,xy,rgb,uv,q->tpage,q->clut); } break;
    default: break;   /* SPRT/TILE/LINE and others: later */
    }
}

DISPENV *GetDispEnv(DISPENV *e) { return e; }
DISPENV *PutDispEnv(DISPENV *e) { if (e && e->disp.w>0) { g_w=e->disp.w; g_h=e->disp.h>0?e->disp.h:g_h; } return e; }
DRAWENV *PutDrawEnv(DRAWENV *e)   /* begin frame: set up 2D ortho + clear */
{ if (e) hostVideoClear(e->r0, e->g0, e->b0); return e; }
void SetDrawArea(DR_AREA *p, RECT *r) { (void)p; (void)r; }
void SetDrawOffset(DR_OFFSET *p, u_short *ofs) { (void)p; (void)ofs; }

/* ---- libetc (host-backed) ---- */
long SetVideoMode(long mode) { hostVideoSetRefresh(mode ? 50 : 60); return mode; }

/* ---- controller input (multitap) ----
 * The engine calls InitTAP with two 34-byte buffers (one per physical port) and
 * reads them as multitap packets. On PSX the BIOS DMAs pad data in; here we fill
 * buffer 0 each frame from the host input (controller 0; others disconnected). */
static unsigned char *g_tap0, *g_tap1;
int port_tap_ok;   /* host-readable: InitTAP captured the buffer */
void InitTAP(void *b0, int l0, void *b1, int l1)
{ (void)l0; (void)l1; g_tap0 = (unsigned char*)b0; g_tap1 = (unsigned char*)b1; port_tap_ok = 1; }
void StartTAP(void) {}

static void port_update_pads(void)
{
    if (g_tap0) {
        unsigned short pad = (unsigned short)~hostInputRead();  /* active-low */
        g_tap0[0] = 0x00; g_tap0[1] = 0x80;     /* packet: status ok, type TAP(8) */
        g_tap0[2] = 0x00; g_tap0[3] = 0x41;     /* tapdata[0]: ok, digital pad */
        g_tap0[4] = pad & 0xff; g_tap0[5] = (pad >> 8) & 0xff;
        g_tap0[10] = g_tap0[18] = g_tap0[26] = 0xff;  /* ports 1-3 disconnected */
    }
    if (g_tap1) g_tap1[0] = 0xff;               /* physical port 1: no device */
}

/* The engine registers MRVBlankCallback here; it drives frame counters/timers.
 * We must call it every VSync or the game's state machine never advances. */
static void (*g_vblank_cb)(void);
int  VSyncCallback(void (*f)(void)) { g_vblank_cb = f; return 0; }
int  VSync(int mode)
{
    (void)mode;
    if (!hostVideoPump()) exit(0);
    hostVideoPresent();
    port_update_pads();               /* refresh the pad buffer this frame */
    if (g_vblank_cb) g_vblank_cb();   /* advance VBlank-driven timing */
    return 0;
}
int  ResetCallback(void) { return 0; }
