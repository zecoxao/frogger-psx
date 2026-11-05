/* gtemac.h -- PsyQ GTE convenience macros (gte_*).
 *
 * In real PsyQ these are inline assembly writing GTE coprocessor registers.
 * On the host we map the common ones onto the software libgte.c functions.
 * This covers the high-level calls; low-level register macros (gte_ldv0,
 * gte_rtps, gte_stsxy ...) should be added here as the build reports them. */
#ifndef PORT_GTEMAC_H
#define PORT_GTEMAC_H
#include "libgte.h"

#define gte_SetRotMatrix(m)     SetRotMatrix(m)
#define gte_SetTransMatrix(m)   SetTransMatrix(m)
#define gte_SetGeomOffset(x, y) SetGeomOffset((x), (y))
#define gte_SetGeomScreen(h)    SetGeomScreen(h)
#define gte_SetBackColor(r,g,b) SetBackColor((r),(g),(b))
#define gte_SetFarColor(r,g,b)  SetFarColor((r),(g),(b))
#define gte_ApplyRotMatrix(v0, v1) ApplyRotMatrix((v0), (v1))

/* The PSX addPrim links primitives into the ordering table using 24-bit
 * addresses -- which truncates real host pointers. Override it to record each
 * primitive by its OT-z slot instead (port_addprim); DrawOTag then draws them
 * back-to-front. This header is included after <libgpu.h>, so the #undef hits. */
#ifdef addPrim
#undef addPrim
#endif
extern void port_addprim(void *otentry, void *prim);
#define addPrim(ot, p) port_addprim((void *)(ot), (void *)(p))

#endif /* PORT_GTEMAC_H */
