/* port_cd.c -- libcd backed by the host FROGPSX.MWD file.
 *
 * The MR resource system computes a sector = rb_root_sector + ri_sector_offset
 * (root sector is 0 here, so offsets are relative to the start of the MWD),
 * seeks with CdControlB(CdlSeekL, loc) and reads with CdRead() into the target
 * buffer. We satisfy that by reading sector*2048 bytes out of FROGPSX.MWD.
 *
 * No PsyQ headers: CdlLOC is 4 bytes and we control CdIntToPos/CdPosToInt, so
 * we pack the raw 32-bit sector across those 4 bytes (internal encoding).
 */
#include <stdio.h>
#include <string.h>

#define CDL_SETLOC 0x02
#define CDL_SEEKL  0x15
#define SECTOR     2048

static FILE *g_mwd;
static long  g_sector;

__attribute__((constructor)) static void cd_open(void)
{
    const char *paths[] = {
        "FROGPSX.MWD",
        "files/FROGPSX.MWD",
        "build/NTSC-U (USA)/files/FROGPSX.MWD",
        0
    };
    int i;
    for (i = 0; paths[i]; ++i) {
        g_mwd = fopen(paths[i], "rb");
        if (g_mwd) { fprintf(stderr, "[cd] opened %s\n", paths[i]); return; }
    }
    fprintf(stderr, "[cd] WARNING: FROGPSX.MWD not found (run from the files/ dir)\n");
}

long CdInit(void) { return 1; }
long CdReset(int m) { (void)m; return 1; }
void CdSetDebug(int m) { (void)m; }
long CdMix(void *v) { (void)v; return 0; }

/* raw-sector packing into the 4-byte CdlLOC */
void *CdIntToPos(int i, void *p)
{
    unsigned char *b = (unsigned char*)p;
    b[0]=(unsigned char)i; b[1]=(unsigned char)(i>>8);
    b[2]=(unsigned char)(i>>16); b[3]=(unsigned char)(i>>24);
    return p;
}
int CdPosToInt(void *p)
{
    unsigned char *b = (unsigned char*)p;
    return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}

/* CdSearchFile(&cdlfile, name): cdlfile.pos is the first member; set it to
 * sector 0 so rb_root_sector becomes 0 (offsets are MWD-relative). */
void *CdSearchFile(void *fp, char *name) { (void)name; if (fp) CdIntToPos(0, fp); return (void*)1; }

static void set_loc(unsigned char com, void *param)
{
    if (param && (com == CDL_SEEKL || com == CDL_SETLOC))
        g_sector = CdPosToInt(param);
}
long CdControl(unsigned char com, void *param, void *result)  { (void)result; set_loc(com, param); return 1; }
long CdControlB(unsigned char com, void *param, void *result) { (void)result; set_loc(com, param); return 1; }

long CdRead(int sectors, void *buf, int mode)
{
    (void)mode;
    if (!g_mwd || sectors <= 0) return 0;
    if (fseek(g_mwd, (long)g_sector * SECTOR, SEEK_SET) != 0) return 0;
    fread(buf, SECTOR, (size_t)sectors, g_mwd);   /* short reads tolerated */
    return 1;
}
long CdRead2(int mode) { (void)mode; return 1; }

/* result[0] holds the CD status byte; 0 => no error. return 0 => complete. */
long CdReadSync(int mode, unsigned char *result) { (void)mode; if (result) result[0] = 0; return 0; }
long CdSync(int mode, unsigned char *result)     { (void)mode; if (result) result[0] = 0; return 2; }

void *CdSyncCallback(void *f)  { (void)f; return 0; }
void *CdReadyCallback(void *f) { (void)f; return 0; }
