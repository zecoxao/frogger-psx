/* port_gte.c -- software implementation of the libgte *function* API.
 *
 * These are the GTE routines the game calls as real functions (matrix build,
 * vector transforms, sin/cos). Signatures match the repo's include/LIBGTE.H.
 * Fixed point: rotation matrix entries are 1.3.12 (value = real * 4096).
 *
 * FIRST CUT -- the algebra is standard but NOT yet validated against hardware.
 * The low-level gte_* register ops live in port_stubs.c (still no-ops); until
 * those + the mesh emitters are real, on-screen geometry is not meaningful.
 */
#include <libgte.h>
#include <libgpu.h>
#define _USE_MATH_DEFINES
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SH 12
#define GTE_ONE 4096

static MATRIX g_rot;
static long   g_tx, g_ty, g_tz;     /* SetTransMatrix */
static long   g_ofx, g_ofy, g_h;    /* SetGeomOffset / SetGeomScreen */

void InitGeom(void) { g_tx=g_ty=g_tz=0; g_ofx=g_ofy=0; g_h=1000; }

void SetRotMatrix(MATRIX *m)   { g_rot = *m; }
void SetTransMatrix(MATRIX *m) { g_tx=m->t[0]; g_ty=m->t[1]; g_tz=m->t[2]; }
void SetGeomOffset(long x, long y) { g_ofx=x; g_ofy=y; }
void SetGeomScreen(long h) { g_h=h; }
void SetBackColor(long r,long g,long b){ (void)r;(void)g;(void)b; }
void SetFarColor(long r,long g,long b){ (void)r;(void)g;(void)b; }
void SetFogNearFar(long a,long b,long h){ (void)a;(void)b;(void)h; }

int rcos(int a){ return (int)(cos((double)a * (2.0*M_PI/4096.0)) * 4096.0); }
int rsin(int a){ return (int)(sin((double)a * (2.0*M_PI/4096.0)) * 4096.0); }

long SquareRoot0(long a){ return a>0 ? (long)(sqrt((double)a)) : 0; }

MATRIX *RotMatrix(SVECTOR *r, MATRIX *m)
{
    long sx=rsin(r->vx), cx=rcos(r->vx);
    long sy=rsin(r->vy), cy=rcos(r->vy);
    long sz=rsin(r->vz), cz=rcos(r->vz);
    long sysx=(sy*sx)>>SH, sycx=(sy*cx)>>SH;
    m->m[0][0]=(short)((cz*cy)>>SH);
    m->m[0][1]=(short)(((cz*sysx)>>SH)-((sz*cx)>>SH));
    m->m[0][2]=(short)(((cz*sycx)>>SH)+((sz*sx)>>SH));
    m->m[1][0]=(short)((sz*cy)>>SH);
    m->m[1][1]=(short)(((sz*sysx)>>SH)+((cz*cx)>>SH));
    m->m[1][2]=(short)(((sz*sycx)>>SH)-((cz*sx)>>SH));
    m->m[2][0]=(short)(-sy);
    m->m[2][1]=(short)((cy*sx)>>SH);
    m->m[2][2]=(short)((cy*cx)>>SH);
    return m;
}

static void matmul(MATRIX *a, MATRIX *b, MATRIX *o)
{
    MATRIX r;
    int i,j,k;
    for(i=0;i<3;i++)for(j=0;j<3;j++){ long s=0; for(k=0;k<3;k++) s+=(long)a->m[i][k]*b->m[k][j]; r.m[i][j]=(short)(s>>SH); }
    r.t[0]=o->t[0]; r.t[1]=o->t[1]; r.t[2]=o->t[2];
    *o=r;
}
MATRIX *MulMatrix0(MATRIX *a, MATRIX *b, MATRIX *o){ matmul(a,b,o); return o; }
MATRIX *MulMatrix(MATRIX *a, MATRIX *b){ matmul(a,b,a); return a; }
MATRIX *MulMatrix2(MATRIX *a, MATRIX *b){ matmul(a,b,b); return b; }

MATRIX *TransposeMatrix(MATRIX *m0, MATRIX *m1)
{
    int i,j; for(i=0;i<3;i++)for(j=0;j<3;j++) m1->m[i][j]=m0->m[j][i];
    return m1;
}

static void applyM(short m[3][3], long x, long y, long z, long *ox,long *oy,long *oz)
{
    *ox=((long)m[0][0]*x+(long)m[0][1]*y+(long)m[0][2]*z)>>SH;
    *oy=((long)m[1][0]*x+(long)m[1][1]*y+(long)m[1][2]*z)>>SH;
    *oz=((long)m[2][0]*x+(long)m[2][1]*y+(long)m[2][2]*z)>>SH;
}
VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1)
{ applyM(m->m, v0->vx,v0->vy,v0->vz, &v1->vx,&v1->vy,&v1->vz); return v1; }
VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{ applyM(m->m, v0->vx,v0->vy,v0->vz, &v1->vx,&v1->vy,&v1->vz); return v1; }
SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1)
{ long a,b,c; applyM(m->m, v0->vx,v0->vy,v0->vz,&a,&b,&c); v1->vx=(short)a;v1->vy=(short)b;v1->vz=(short)c; return v1; }
VECTOR *ApplyRotMatrix(SVECTOR *v0, VECTOR *v1)
{ applyM(g_rot.m, v0->vx,v0->vy,v0->vz, &v1->vx,&v1->vy,&v1->vz); return v1; }
VECTOR *ApplyTransposeMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
    v1->vx=((long)m->m[0][0]*v0->vx+(long)m->m[1][0]*v0->vy+(long)m->m[2][0]*v0->vz)>>SH;
    v1->vy=((long)m->m[0][1]*v0->vx+(long)m->m[1][1]*v0->vy+(long)m->m[2][1]*v0->vz)>>SH;
    v1->vz=((long)m->m[0][2]*v0->vx+(long)m->m[1][2]*v0->vy+(long)m->m[2][2]*v0->vz)>>SH;
    return v1;
}

static long vnorm(long x,long y,long z,double *inv)
{
    double len=sqrt((double)x*x+(double)y*y+(double)z*z);
    if(len<1e-6){*inv=0;return 0;} *inv=4096.0/len; return (long)len;
}
long VectorNormal(VECTOR *v0, VECTOR *v1)
{ double k; long l=vnorm(v0->vx,v0->vy,v0->vz,&k); v1->vx=(long)(v0->vx*k);v1->vy=(long)(v0->vy*k);v1->vz=(long)(v0->vz*k); return l; }
long VectorNormalS(VECTOR *v0, SVECTOR *v1)
{ double k; long l=vnorm(v0->vx,v0->vy,v0->vz,&k); v1->vx=(short)(v0->vx*k);v1->vy=(short)(v0->vy*k);v1->vz=(short)(v0->vz*k); return l; }
long VectorNormalSS(SVECTOR *v0, SVECTOR *v1)
{ double k; long l=vnorm(v0->vx,v0->vy,v0->vz,&k); v1->vx=(short)(v0->vx*k);v1->vy=(short)(v0->vy*k);v1->vz=(short)(v0->vz*k); return l; }

void OuterProduct12(VECTOR *v0, VECTOR *v1, VECTOR *v2)
{
    v2->vx=(v0->vy*v1->vz - v0->vz*v1->vy)>>SH;
    v2->vy=(v0->vz*v1->vx - v0->vx*v1->vz)>>SH;
    v2->vz=(v0->vx*v1->vy - v0->vy*v1->vx)>>SH;
}

/* ========================================================================
 * Software GTE register machine (the gte_* inline ops).
 *
 * The mesh emitters (MR_P_*.C) drive these: load vertices, rotate/project,
 * read back screen xy + ordering-table z. Models the PSX SXY (3-deep) and SZ
 * (4-deep) FIFOs. Lighting ops pass the loaded colour through (no dynamic
 * light yet). Shares the rotation/translation/projection state above.
 * ======================================================================== */

static short g_vx[3], g_vy[3], g_vz[3];     /* loaded vectors V0..V2 */
static short g_sx[3], g_sy[3];              /* SXY FIFO (0=oldest)   */
static long  g_sz[4];                       /* SZ FIFO  (3=newest)   */
static long  g_mac0;                        /* nclip / opz result    */
static long  g_otz;                         /* average z             */
static unsigned char g_r, g_gg, g_b, g_cd;  /* RGB register          */

static void gte_project(short vx, short vy, short vz, short *osx, short *osy, long *oz)
{
    long x =((long)g_rot.m[0][0]*vx+(long)g_rot.m[0][1]*vy+(long)g_rot.m[0][2]*vz)>>SH;
    long y =((long)g_rot.m[1][0]*vx+(long)g_rot.m[1][1]*vy+(long)g_rot.m[1][2]*vz)>>SH;
    long z =((long)g_rot.m[2][0]*vx+(long)g_rot.m[2][1]*vy+(long)g_rot.m[2][2]*vz)>>SH;
    x += g_tx; y += g_ty; z += g_tz;
    if (z < 1) z = 1;                       /* avoid div0 / behind camera */
    *osx = (short)(g_ofx + x * g_h / z);
    *osy = (short)(g_ofy + y * g_h / z);
    *oz  = z;
}

static void sz_push(long z){ g_sz[0]=g_sz[1]; g_sz[1]=g_sz[2]; g_sz[2]=g_sz[3]; g_sz[3]=z; }
static void sxy_push(short x, short y){ g_sx[0]=g_sx[1]; g_sx[1]=g_sx[2]; g_sx[2]=x;
                                        g_sy[0]=g_sy[1]; g_sy[1]=g_sy[2]; g_sy[2]=y; }

void gte_ldv0(SVECTOR *v){ g_vx[0]=v->vx; g_vy[0]=v->vy; g_vz[0]=v->vz; }
void gte_ldv1(SVECTOR *v){ g_vx[1]=v->vx; g_vy[1]=v->vy; g_vz[1]=v->vz; }
void gte_ldv2(SVECTOR *v){ g_vx[2]=v->vx; g_vy[2]=v->vy; g_vz[2]=v->vz; }
void gte_ldv3(SVECTOR *a, SVECTOR *b, SVECTOR *c)
{ gte_ldv0(a); gte_ldv1(b); gte_ldv2(c); }

void gte_rtps(void)   /* transform V0, push one onto the FIFOs */
{ short sx,sy; long z; gte_project(g_vx[0],g_vy[0],g_vz[0],&sx,&sy,&z); sxy_push(sx,sy); sz_push(z); }

void gte_rtpt(void)   /* transform V0,V1,V2 -> fill the 3-deep FIFO */
{
    long z;
    gte_project(g_vx[0],g_vy[0],g_vz[0], &g_sx[0],&g_sy[0], &z); g_sz[1]=z;
    gte_project(g_vx[1],g_vy[1],g_vz[1], &g_sx[1],&g_sy[1], &z); g_sz[2]=z;
    gte_project(g_vx[2],g_vy[2],g_vz[2], &g_sx[2],&g_sy[2], &z); g_sz[3]=z;
}

static long pack_sxy(int i){ return ((long)(unsigned short)g_sy[i] << 16) | (unsigned short)g_sx[i]; }
void gte_stsxy (long *p){ *p = pack_sxy(2); }
void gte_stsxy0(long *p){ *p = pack_sxy(0); }
void gte_stsxy1(long *p){ *p = pack_sxy(1); }
void gte_stsxy2(long *p){ *p = pack_sxy(2); }
void gte_stsxy3(long *a, long *b, long *c){ *a=pack_sxy(0); *b=pack_sxy(1); *c=pack_sxy(2); }
void gte_stsxy3_f3(POLY_F3 *p)
{ p->x0=g_sx[0]; p->y0=g_sy[0]; p->x1=g_sx[1]; p->y1=g_sy[1]; p->x2=g_sx[2]; p->y2=g_sy[2]; }

void gte_stsz (long *p){ *p = g_sz[3]; }
void gte_stotz(long *p){ *p = g_otz; }
void gte_stopz(long *p){ *p = g_mac0; }

void gte_nclip(void)   /* 2D cross product of the 3 FIFO points (winding) */
{ g_mac0 = (long)(g_sx[1]-g_sx[0])*(g_sy[2]-g_sy[0]) - (long)(g_sx[2]-g_sx[0])*(g_sy[1]-g_sy[0]); }

void gte_avsz3(void){ g_otz = (g_sz[1]+g_sz[2]+g_sz[3])/3; }
void gte_avsz4(void){ g_otz = (g_sz[0]+g_sz[1]+g_sz[2]+g_sz[3])/4; }

void gte_ldrgb(CVECTOR *c){ g_r=c->r; g_gg=c->g; g_b=c->b; g_cd=c->cd; }
void gte_strgb(CVECTOR *c){ c->r=g_r; c->g=g_gg; c->b=g_b; }   /* cd/code left as set */
void gte_ncds(void){}   /* lighting: pass loaded colour through (no dynamic light yet) */
void gte_nccs(void){}
/* Store MAC3 (non-limited view-space Z) from the preceding gte_rtps. The MR
 * local-OT path (MRStaticDisplayMeshInstance) uses this as the mesh origin's
 * view z to place its OT; stubbing it to 0 made every such mesh fail the
 * "i < MR_OT_NEAR_CLIP" test and vanish (e.g. the select-level preview cubes). */
void gte_stlvnl2(long *p){ *p = g_sz[3]; }

/* Lighting/colour matrix setters -- no-ops until lighting is implemented. */
void gte_SetColorMatrix(MATRIX *m){ (void)m; }
void gte_SetLightMatrix(MATRIX *m){ (void)m; }
void gte_MulMatrix0(void){}

/* ---- gouraud / lighting / env-map ops ----
 * No dynamic lighting yet: colours pass through as the loaded RGB (flat), and
 * the per-type store ops just copy screen xy / RGB into the right prim layout. */
void gte_rtv0(SVECTOR *v){ (void)v; }        /* rotate normal: unused w/o lighting */
void gte_stlvnl(long *p){ if (p) *p = ONE; } /* local-vector normal length */
void gte_ncdt(void){}   /* 3-vertex colour from normals: keep loaded RGB */
void gte_ncct(void){}

static void put3rgb(unsigned char *r0,unsigned char *g0,unsigned char *b0,int stride,int n)
{ int i; for(i=0;i<n;i++){ r0[i*stride]=g_r; g0[i*stride]=g_gg; b0[i*stride]=g_b; } }

void gte_strgb3_g3(POLY_G3 *p){ p->r0=g_r;p->g0=g_gg;p->b0=g_b; p->r1=g_r;p->g1=g_gg;p->b1=g_b; p->r2=g_r;p->g2=g_gg;p->b2=g_b; }
void gte_strgb3_g4(POLY_G4 *p){ p->r0=g_r;p->g0=g_gg;p->b0=g_b; p->r1=g_r;p->g1=g_gg;p->b1=g_b; p->r2=g_r;p->g2=g_gg;p->b2=g_b; p->r3=g_r;p->g3=g_gg;p->b3=g_b; }
void gte_strgb3_gt3(POLY_GT3 *p){ p->r0=g_r;p->g0=g_gg;p->b0=g_b; p->r1=g_r;p->g1=g_gg;p->b1=g_b; p->r2=g_r;p->g2=g_gg;p->b2=g_b; }
void gte_strgb3_gt4(POLY_GT4 *p){ p->r0=g_r;p->g0=g_gg;p->b0=g_b; p->r1=g_r;p->g1=g_gg;p->b1=g_b; p->r2=g_r;p->g2=g_gg;p->b2=g_b; p->r3=g_r;p->g3=g_gg;p->b3=g_b; }

void gte_stsxy3_g3 (POLY_G3 *p) { p->x0=g_sx[0];p->y0=g_sy[0]; p->x1=g_sx[1];p->y1=g_sy[1]; p->x2=g_sx[2];p->y2=g_sy[2]; }
void gte_stsxy3_ft3(POLY_FT3 *p){ p->x0=g_sx[0];p->y0=g_sy[0]; p->x1=g_sx[1];p->y1=g_sy[1]; p->x2=g_sx[2];p->y2=g_sy[2]; }
void gte_stsxy3_gt3(POLY_GT3 *p){ p->x0=g_sx[0];p->y0=g_sy[0]; p->x1=g_sx[1];p->y1=g_sy[1]; p->x2=g_sx[2];p->y2=g_sy[2]; }
