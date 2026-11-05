/* port_audio.c -- PSX sound (libspu/libsnd Ss* layer) on SDL audio.
 *
 * The MR engine allocates 24 voices itself and drives them with the libsnd
 * "utility" calls: load a VAB (sample bank), then SsUtKeyOnV(voice, vab, prog,
 * tone, note, ...) to play a sample at a pitch. We implement that:
 *   - a 512 KB "SPU RAM" that VAB bodies (ADPCM) are copied into,
 *   - VAB header parsing (program/tone -> sample address + center note + ADSR),
 *   - PSX 4-bit ADPCM decode (decoded to PCM on key-on),
 *   - a 24-voice mixer resampling by pitch, summed to an SDL audio stream.
 * Music (XA/CD streaming) is separate and still stubbed.
 *
 * FIRST CUT: pitch uses the 2^((note-center)/12) approximation and ADSR is a
 * simplified gate+release, so timbre/pitch may be slightly off. Compiled with
 * host headers (in porthost), no PsyQ headers -> K&R-compatible signatures.
 */
#include <SDL.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define OUT_FREQ   44100
#define SPU_RAM_SZ (512*1024)
#define NVOICE     24
#define MAX_PCM    (OUT_FREQ*2)      /* cap a decoded sample at 2 s */
#define SPU_ON  1
#define SPU_OFF 0

static unsigned char spu_ram[SPU_RAM_SZ];
static int           spu_top;        /* next free SPU-RAM offset */

/* ---- VAB (sample bank) ---- */
typedef struct { int addr; int center; int shift; unsigned short adsr1, adsr2; } Tone;
typedef struct {
    int   used;
    int   base;                      /* SPU-RAM base of this VAB's body */
    Tone  tone[128][16];             /* [program][tone] */
} Vab;
static Vab g_vab[16];

/* ---- voice ---- */
typedef struct {
    volatile int active;
    short  *pcm; int pcm_len; int loop_start;   /* -1 = one-shot */
    double  pos, step;
    float   gain_l, gain_r;
    int     released; float env;
} Voice;
static Voice g_voice[NVOICE];

static SDL_AudioDeviceID g_dev;
static float g_mvol = 1.0f;
static FILE *g_dump;                 /* optional PORT_AUDIO_DUMP raw s16 */

/* ---- PSX ADPCM decode: ram[addr..] -> out[], until end flag ---- */
static const int ADF0[] = {0,60,115,98,122};
static const int ADF1[] = {0,0,-52,-55,-60};
static int decode_adpcm(int addr, short *out, int *loop_start)
{
    int prev1 = 0, prev2 = 0, n = 0, i, nib;
    *loop_start = -1;
    while (n + 28 <= MAX_PCM && addr + 16 <= SPU_RAM_SZ) {
        unsigned char *b = &spu_ram[addr];
        int shift = b[0] & 0x0f, filter = (b[0] >> 4) & 0x07, flags = b[1];
        if (filter > 4) filter = 0;
        if (flags & 0x04) *loop_start = n;             /* loop start */
        for (i = 0; i < 14; ++i) {
            unsigned char by = b[2+i];
            for (nib = 0; nib < 2; ++nib) {
                int s = nib ? (by >> 4) : (by & 0x0f);
                int smp = (short)(s << 12);            /* sign-extend 4-bit */
                smp >>= shift;
                smp += (prev1*ADF0[filter] + prev2*ADF1[filter]) >> 6;
                if (smp > 32767) smp = 32767; if (smp < -32768) smp = -32768;
                out[n++] = (short)smp; prev2 = prev1; prev1 = smp;
            }
        }
        addr += 16;
        if (flags & 0x01) {                            /* end block */
            if (!(flags & 0x02)) *loop_start = -1;     /* not a loop: one-shot */
            break;
        }
    }
    return n;
}

/* ---- SDL mix callback: sum active voices ---- */
static void mix(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    short *out = (short*)stream;
    int frames = len / (2 * (int)sizeof(short)), i, v;
    memset(stream, 0, len);
    for (i = 0; i < frames; ++i) {
        float l = 0, r = 0;
        for (v = 0; v < NVOICE; ++v) {
            Voice *vo = &g_voice[v];
            if (!vo->active || !vo->pcm) continue;
            int idx = (int)vo->pos;
            if (idx >= vo->pcm_len) {
                if (vo->loop_start >= 0) { vo->pos = vo->loop_start; idx = (int)vo->pos; }
                else { vo->active = 0; continue; }
            }
            float s = vo->pcm[idx] * vo->env;
            l += s * vo->gain_l; r += s * vo->gain_r;
            if (vo->released) { vo->env -= 1.0f/(OUT_FREQ*0.08f); if (vo->env <= 0) vo->active = 0; }
            vo->pos += vo->step;
        }
        l *= g_mvol; r *= g_mvol;
        if (l > 32767) l = 32767; if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; if (r < -32768) r = -32768;
        out[i*2] = (short)l; out[i*2+1] = (short)r;
    }
    if (g_dump) fwrite(stream, 1, len, g_dump);
}

/* ======================= libsnd / libspu API ======================= */

void SsInit(void)
{
    if (g_dev) return;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr,"[audio] init failed: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = OUT_FREQ; want.format = AUDIO_S16SYS; want.channels = 2;
    want.samples = 1024; want.callback = mix;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) { fprintf(stderr,"[audio] open failed: %s\n", SDL_GetError()); return; }
    if (getenv("PORT_AUDIO_DUMP")) g_dump = fopen(getenv("PORT_AUDIO_DUMP"), "wb");
    SDL_PauseAudioDevice(g_dev, 0);
    fprintf(stderr, "[audio] SDL audio up: %d Hz stereo\n", have.freq);
}
void SsStart(void) {}
void SsEnd(void) {}
void SsQuit(void) {}
void SsSetTickMode(int m) { (void)m; }
void SsSetSerialAttr(int a, int b, int c) { (void)a;(void)b;(void)c; }
void SsSetSerialVol(int s, int l, int r) { (void)s;(void)l;(void)r; }
void SsSetMVol(int l, int r) { g_mvol = ((l>r?l:r) / 127.0f); if (g_mvol>1) g_mvol=1; }
void SpuInit(void) {}
void SpuSetCommonMasterVolume(short l, short r) { (void)l;(void)r; }

/* VAB header offsets (fixed layout). */
#define VH_PROG_OFF   32
#define VH_TONE_OFF   (32 + 128*16)
#define VH_VAGTAB_OFF (VH_TONE_OFF + 128*16*32)
static unsigned short rd16(const unsigned char *p){ return p[0] | (p[1]<<8); }

static int g_vh_vag[16][257];        /* cached VAG offset table per vab */

short SsVabOpenHead(unsigned char *vh, short vabid)
{
    int id = (vabid >= 0 && vabid < 16) ? vabid : -1, i, p, t;
    if (id < 0) for (i = 0; i < 16; ++i) if (!g_vab[i].used) { id = i; break; }
    if (id < 0 || !vh) return -1;
    Vab *V = &g_vab[id];
    memset(V, 0, sizeof *V); V->used = 1;
    const unsigned char *vagtab = vh + VH_VAGTAB_OFF;
    for (i = 0; i < 256; ++i) g_vh_vag[id][i] = rd16(vagtab + i*2);
    for (p = 0; p < 128; ++p)
        for (t = 0; t < 16; ++t) {
            const unsigned char *a = vh + VH_TONE_OFF + (p*16 + t)*32;
            int vag = rd16(a + 22);                    /* 1-based VAG index */
            V->tone[p][t].center = a[4];
            V->tone[p][t].shift  = a[5];
            V->tone[p][t].adsr1  = rd16(a + 16);
            V->tone[p][t].adsr2  = rd16(a + 18);
            V->tone[p][t].addr   = vag;                /* resolved after TransBody */
        }
    return (short)id;
}

short SsVabTransBody(unsigned char *vb, short vabid)
{
    if (vabid < 0 || vabid >= 16 || !g_vab[vabid].used || !vb) return -1;
    Vab *V = &g_vab[vabid];
    V->base = spu_top;
    /* body size isn't passed; copy up to the remaining SPU RAM, bounded by the
     * sum of VAG sizes in the offset table. */
    int total = 0, i;
    for (i = 1; i < 256; ++i) total += g_vh_vag[vabid][i] * 8;
    if (total <= 0 || total > SPU_RAM_SZ - spu_top) total = SPU_RAM_SZ - spu_top;
    memcpy(&spu_ram[spu_top], vb, total);
    /* resolve each tone's VAG (1-based) to an absolute SPU-RAM address */
    int cum[257]; cum[0] = 0;
    for (i = 1; i < 257; ++i) cum[i] = cum[i-1] + g_vh_vag[vabid][i-1]*8;
    int p, t;
    for (p = 0; p < 128; ++p) for (t = 0; t < 16; ++t) {
        int vag = V->tone[p][t].addr;                  /* 1-based */
        V->tone[p][t].addr = (vag >= 1) ? (V->base + cum[vag-1]) : -1;
    }
    spu_top += total;
    fprintf(stderr, "[audio] VAB %d loaded: %d bytes at SPU 0x%x\n", vabid, total, V->base);
    return vabid;
}
short SsVabTransCompleted(short f) { (void)f; return 1; }
void  SsVabClose(short vabid) { if (vabid>=0 && vabid<16) g_vab[vabid].used = 0; }

void SsUtKeyOnV(short vc, short vabid, short prog, short tone, short note,
                short fine, short voll, short volr)
{
    if (vc < 0 || vc >= NVOICE || vabid < 0 || vabid >= 16) return;
    if (!g_vab[vabid].used) return;
    Tone *T = &g_vab[vabid].tone[prog & 127][tone & 15];
    if (T->addr < 0) return;
    static short tmp[MAX_PCM];
    int loop, n = decode_adpcm(T->addr, tmp, &loop);
    if (n <= 0) return;
    Voice *vo = &g_voice[vc];
    SDL_LockAudioDevice(g_dev);
    vo->pcm = (short*)realloc(vo->pcm, n * sizeof(short));
    memcpy(vo->pcm, tmp, n * sizeof(short));
    vo->pcm_len = n; vo->loop_start = loop;
    double semis = (note - T->center) + fine/128.0;
    vo->step   = pow(2.0, semis/12.0);
    vo->pos    = 0; vo->env = 1.0f; vo->released = 0;
    vo->gain_l = (voll & 0x7f) / 127.0f;
    vo->gain_r = (volr & 0x7f) / 127.0f;
    vo->active = 1;
    SDL_UnlockAudioDevice(g_dev);
}
void SsUtKeyOffV(short vc) { if (vc>=0 && vc<NVOICE) g_voice[vc].released = 1; }

void SsUtSetVVol(short vc, short l, short r)
{ if (vc>=0 && vc<NVOICE) { g_voice[vc].gain_l=(l&0x7f)/127.0f; g_voice[vc].gain_r=(r&0x7f)/127.0f; } }
void SsUtChangePitch(short vc, short vab, short prog, short note, short fine, short np, short of)
{ (void)vab;(void)prog;(void)of; if (vc>=0 && vc<NVOICE) { Vab*V=&g_vab[vab&15]; int c=V->tone[prog&127][0].center;
    g_voice[vc].step = pow(2.0, ((np?np:note)-c + fine/128.0)/12.0); } }
void SsUtPitchBend(short vc, short vab, short prog, short note, short bend)
{ (void)vab;(void)prog;(void)note; if (vc>=0 && vc<NVOICE) g_voice[vc].step *= pow(2.0, (bend/8192.0)*2.0/12.0); }
void SsUtChangeADSR(short vc, short vab, short prog, short note, short a1, short a2)
{ (void)vc;(void)vab;(void)prog;(void)note;(void)a1;(void)a2; }

long SpuGetAllKeysStatus(char *status)
{
    int v;
    for (v = 0; v < NVOICE; ++v) status[v] = g_voice[v].active ? SPU_ON : SPU_OFF;
    return 0;
}
