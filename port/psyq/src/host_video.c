/* host_video.c -- SDL2 + OpenGL host surface for the PsyQ shim.
 *
 * Milestone 1: opens a window, runs the event/present loop, maps input.
 * It does NOT yet rasterise PSX primitives (milestone 3) -- it only clears
 * to a background colour so you can confirm the loop and timing are alive.
 */
#include "host_video.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

/* The PSX runs entirely from writable RAM, so the game freely writes into what
 * the C compiler puts in read-only .rodata (e.g. string literals it edits in
 * place). On Linux those pages are read-only and such writes segfault. Emulate
 * the console by making the process's read-only data pages writable at startup. */
__attribute__((constructor)) static void port_unprotect_rodata(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        unsigned long a, b; char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &a, &b, perms) == 3 &&
            perms[0] == 'r' && perms[1] == '-' && perms[2] == '-')
            mprotect((void*)a, b - a, PROT_READ | PROT_WRITE);  /* best effort */
    }
    fclose(f);
}

/* Temporary crash diagnostics: print a backtrace on SIGSEGV so we can see
 * where the stubbed runtime first falls over. Auto-installed at startup. */
static void port_segv(int sig)
{
    void *bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "\n[host] caught signal %d; backtrace:\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(139);
}
__attribute__((constructor)) static void port_install_segv(void)
{
    setvbuf(stderr, NULL, _IONBF, 0);   /* unbuffered: debug survives _exit */
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGSEGV, port_segv);
    signal(SIGBUS, port_segv);
    signal(SIGALRM, port_segv);   /* watchdog: dump where we are after N sec */
    if (getenv("PORT_WATCHDOG")) alarm((unsigned)atoi(getenv("PORT_WATCHDOG")));
}

/* Host debug printf usable from game TUs whose -include stdio.h resolves to the
 * PsyQ stub (no stderr/fprintf). Declared there as `extern void port_dbgf(...)`. */
#include <stdarg.h>
void port_dbgf(const char *fmt, ...)
{
    va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a);
}

static SDL_Window   *g_win;
static SDL_GLContext g_gl;
static SDL_GameController *g_pad;
static int  g_refresh_hz = 60;
static int  g_fps_override = 0;    /* PORT_FPS / F1 toggle overrides SetVideoMode */
static int  g_logical_w = 320, g_logical_h = 240;
static unsigned char g_cr = 0, g_cg = 0, g_cb = 0;
static Uint64 g_last_present;      /* perf-counter ticks */
static unsigned long g_pad_bits;

int hostVideoInit(int w, int h)
{
    if (g_win) return 0; /* already up */
    g_logical_w = w; g_logical_h = h;
    if (getenv("PORT_FPS")) { g_refresh_hz = atoi(getenv("PORT_FPS")); g_fps_override = 1; }

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");  /* keep our watchdog/segv handlers */
    /* DualShock 4 / DualSense: enable SDL's HIDAPI PS controller drivers so the
     * pad is recognised over USB and Bluetooth with correct button mapping. */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[host] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    /* Present the PSX framebuffer at an integer-ish scale for crispness. */
    int scale = 2;
    g_win = SDL_CreateWindow("Frogger (PC port - scaffold)",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             w * scale, h * scale, SDL_WINDOW_OPENGL);
    if (!g_win) {
        fprintf(stderr, "[host] CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }
    g_gl = SDL_GL_CreateContext(g_win);
    if (!g_gl) {
        fprintf(stderr, "[host] GL context failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetSwapInterval(0); /* don't block on vblank; frame cap paces us */

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); break; }
    }
    g_last_present = SDL_GetPerformanceCounter();
    fprintf(stderr, "[host] video up: %dx%d logical\n", w, h);
    return 0;
}

void hostVideoShutdown(void)
{
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (g_gl)  SDL_GL_DeleteContext(g_gl);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
    g_win = NULL; g_gl = NULL; g_pad = NULL;
}

void hostVideoSetRefresh(int hz) { if (hz > 0 && !g_fps_override) g_refresh_hz = hz; }
void hostVideoSetClear(unsigned char r, unsigned char g, unsigned char b)
{ g_cr = r; g_cg = g; g_cb = b; }

/* Standard PSX digital-pad bit layout (active-high here; the pad buffer stores
 * ~bits, active-low). bit3=Start bit0=Select; dpad 4-7; face 12-15; L/R 8-11. */
#define PB_SELECT 0x0001
#define PB_START  0x0008
#define PB_UP     0x0010
#define PB_RIGHT  0x0020
#define PB_DOWN   0x0040
#define PB_LEFT   0x0080
#define PB_L2     0x0100
#define PB_R2     0x0200
#define PB_L1     0x0400
#define PB_R1     0x0800
#define PB_TRI    0x1000
#define PB_CIRCLE 0x2000
#define PB_CROSS  0x4000
#define PB_SQUARE 0x8000

static unsigned long keyboard_bits(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    unsigned long b = 0;
    if (k[SDL_SCANCODE_UP])     b |= PB_UP;
    if (k[SDL_SCANCODE_DOWN])   b |= PB_DOWN;
    if (k[SDL_SCANCODE_LEFT])   b |= PB_LEFT;
    if (k[SDL_SCANCODE_RIGHT])  b |= PB_RIGHT;
    if (k[SDL_SCANCODE_RETURN]) b |= PB_START;
    if (k[SDL_SCANCODE_RSHIFT]) b |= PB_SELECT;
    if (k[SDL_SCANCODE_X])      b |= PB_CROSS;
    if (k[SDL_SCANCODE_C])      b |= PB_CIRCLE;
    if (k[SDL_SCANCODE_Z])      b |= PB_SQUARE;
    if (k[SDL_SCANCODE_S])      b |= PB_TRI;
    return b;
}

static unsigned long controller_bits(void)
{
    if (!g_pad) return 0;
    SDL_GameController *c = g_pad;
    unsigned long b = 0;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP))    b |= PB_UP;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  b |= PB_DOWN;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  b |= PB_LEFT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= PB_RIGHT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START))      b |= PB_START;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK))       b |= PB_SELECT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A))          b |= PB_CROSS;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B))          b |= PB_CIRCLE;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X))          b |= PB_SQUARE;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y))          b |= PB_TRI;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  b |= PB_L1;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= PB_R1;
    /* triggers (DS4 L2/R2 are axes) */
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > 8000) b |= PB_L2;
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) b |= PB_R2;
    /* left analog stick -> D-pad (so stick works for movement) */
    { int ax = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
      int ay = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
      if (ax < -12000) b |= PB_LEFT; else if (ax > 12000) b |= PB_RIGHT;
      if (ay < -12000) b |= PB_UP;   else if (ay > 12000) b |= PB_DOWN; }
    return b;
}

int hostVideoPump(void)
{
    SDL_Event e;
    int running = 1;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) running = 0;
        else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
        else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1) {
            /* cycle the frame-rate cap: 60 -> 120 -> uncapped -> 60 */
            g_fps_override = 1;
            g_refresh_hz = (g_refresh_hz == 60) ? 120 : (g_refresh_hz == 120) ? 0 : 60;
            fprintf(stderr, "[fps] cap = %s\n", g_refresh_hz ? (g_refresh_hz==120?"120":"60") : "uncapped");
        }
        else if (e.type == SDL_CONTROLLERDEVICEADDED && !g_pad)
            g_pad = SDL_GameControllerOpen(e.cdevice.which);
    }
    /* PSX pad bits are active-low in PadRead; the game inverts. We hand out
     * the pressed-bit set and invert in PadRead(). */
    unsigned long autob = 0;
    if (getenv("PORT_AUTO")) {           /* page-aware path into a level */
        extern int port_cur_page; static long pf = 0; pf++;
        static int last_page = -1, page_frames = 0;
        if (port_cur_page != last_page) { last_page = port_cur_page; page_frames = 0; }
        else page_frames++;
        /* 0x6 = main menu: tap Cross ~every 2.5s to advance into level select. */
        if (port_cur_page == 0x6 && (pf % 150) < 5)
            autob |= PB_CROSS;
        /* 0x8 = select level: after settling, tap Cross periodically to confirm
         * the highlighted level and drop into gameplay. PORT_AUTO=2 enables this
         * leg; PORT_AUTO=1 stays on select-level. */
        if (port_cur_page == 0x8 && atoi(getenv("PORT_AUTO")) >= 2 &&
            page_frames >= 120 && (page_frames % 90) < 4)
            autob |= PB_CROSS;
    }
    g_pad_bits = controller_bits() | keyboard_bits() | autob;
    return running;
}

unsigned long hostInputRead(void) { return g_pad_bits; }

/* Begin a frame: set up a 2D ortho matching the PSX screen (y-down) and clear.
 * Called from PutDrawEnv. Primitives are then drawn in painter's order. */
void hostVideoClear(unsigned char r, unsigned char g, unsigned char b)
{
    if (!g_win) return;
    int ww, hh;
    SDL_GL_GetDrawableSize(g_win, &ww, &hh);
    glViewport(0, 0, ww, hh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)g_logical_w, (double)g_logical_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* ---- PSX VRAM texture sampling ----
 * Each (tpage,clut) combo is decoded from the emulated VRAM into a 256x256
 * RGBA GL texture and cached; the cache is dropped when VRAM changes. */
extern unsigned short port_vram[];
extern int            port_vram_gen;

typedef struct { int key; GLuint tex; } TexEntry;
#define TEX_CACHE_MAX 1024
static TexEntry g_texcache[TEX_CACHE_MAX];
static int      g_texcount;
static int      g_texgen = -1;

static unsigned vram_rgba(unsigned short c)  /* PSX 1555 (STP|B5|G5|R5) -> RGBA8 */
{
    if (c == 0) return 0;                     /* 0x0000 is fully transparent */
    unsigned r = (c & 0x1f) << 3;
    unsigned g = ((c >> 5) & 0x1f) << 3;
    unsigned b = ((c >> 10) & 0x1f) << 3;
    return r | (g << 8) | (b << 16) | (0xffu << 24);
}

static GLuint decode_texture(int tpage, int clut)
{
    int tp  = (tpage >> 7) & 3;               /* 0=4bit 1=8bit 2,3=16bit */
    int px  = (tpage & 0xf) * 64;             /* page base X (VRAM pixels) */
    int py  = ((tpage >> 4) & 1) * 256;       /* page base Y */
    int clx = (clut & 0x3f) * 16;             /* CLUT X */
    int cly = (clut >> 6) & 0x1ff;            /* CLUT Y */
    static unsigned rgba[256*256];
    int u, v;
    for (v = 0; v < 256; ++v) {
        unsigned short *row = &port_vram[((py+v) & 511) * 1024];
        for (u = 0; u < 256; ++u) {
            unsigned short texel, c;
            if (tp == 0) {                    /* 4-bit: 4 texels/word, CLUT16 */
                texel = row[(px + (u>>2)) & 1023];
                int idx = (texel >> ((u & 3) * 4)) & 0xf;
                c = port_vram[(cly & 511) * 1024 + ((clx + idx) & 1023)];
            } else if (tp == 1) {             /* 8-bit: 2 texels/word, CLUT256 */
                texel = row[(px + (u>>1)) & 1023];
                int idx = (texel >> ((u & 1) * 8)) & 0xff;
                c = port_vram[(cly & 511) * 1024 + ((clx + idx) & 1023)];
            } else {                          /* 16-bit direct */
                c = row[(px + u) & 1023];
            }
            rgba[v*256 + u] = vram_rgba(c);
        }
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return tex;
}

static GLuint get_texture(int tpage, int clut)
{
    int key = ((tpage & 0x1ff) << 16) | (clut & 0xffff), i;
    if (g_texgen != port_vram_gen) {          /* VRAM changed: drop cache */
        for (i = 0; i < g_texcount; ++i) glDeleteTextures(1, &g_texcache[i].tex);
        g_texcount = 0; g_texgen = port_vram_gen;
    }
    for (i = 0; i < g_texcount; ++i) if (g_texcache[i].key == key) return g_texcache[i].tex;
    GLuint tex = decode_texture(tpage, clut);
    if (g_texcount < TEX_CACHE_MAX) { g_texcache[g_texcount].key = key; g_texcache[g_texcount].tex = tex; g_texcount++; }
    return tex;
}

void hostVideoPoly(int n, const int *xy, const unsigned char *rgb,
                   const unsigned char *uv, int tpage, int clut)
{
    if (!g_win || (n != 3 && n != 4)) return;
    static const int tri3[] = {0,1,2};
    static const int quad[] = {0,1,2, 1,3,2};
    const int *idx = (n==3) ? tri3 : quad;
    int count = (n==3) ? 3 : 6, i;

    if (uv) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, get_texture(tpage, clut));
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);        /* drop transparent texels */
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
    }
    glBegin(GL_TRIANGLES);
    for (i = 0; i < count; ++i) {
        int v = idx[i];
        glColor3ub(rgb[v*3+0], rgb[v*3+1], rgb[v*3+2]);
        if (uv) glTexCoord2f((uv[v*2+0] + 0.5f) / 256.0f, (uv[v*2+1] + 0.5f) / 256.0f);
        glVertex2i(xy[v*2+0], xy[v*2+1]);
    }
    glEnd();
}

extern int port_last_prims, port_cnt_sprt, port_cnt_tile, port_cnt_tpage, port_tap_ok;
int port_cur_page;
static void host_screenshot(void)
{
    const char *path = getenv("PORT_SHOT"); if (!path) return;
    fprintf(stderr, "[host] prims=%d sprt=%d tile=%d tpage=%d  tap_init=%d page=0x%x\n",
            port_last_prims, port_cnt_sprt, port_cnt_tile, port_cnt_tpage, port_tap_ok, port_cur_page);
    int ww, hh; SDL_GL_GetDrawableSize(g_win, &ww, &hh);
    unsigned char *px = (unsigned char*)malloc((size_t)ww*hh*3);
    if (!px) return;
    glReadPixels(0, 0, ww, hh, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", ww, hh);
        /* GL origin is bottom-left; flip vertically */
        for (int y = hh-1; y >= 0; --y) fwrite(px + (size_t)y*ww*3, 1, (size_t)ww*3, f);
        fclose(f);
        fprintf(stderr, "[host] wrote screenshot %s (%dx%d)\n", path, ww, hh);
    }
    free(px);
}

void hostVideoPresent(void)
{
    if (!g_win) return;
    { static long fc=0; long want = getenv("PORT_SHOT_FRAME")?atol(getenv("PORT_SHOT_FRAME")):180;
      if (++fc == want) host_screenshot(); }
    /* PORT_PAGE_SHOW: log the option-page id + prim count whenever it changes */
    if (getenv("PORT_PAGE_SHOW")) { static int last = -999;
        if (port_cur_page != last) {
            fprintf(stderr, "[page] 0x%x (prims=%d sprt=%d)\n", port_cur_page, port_last_prims, port_cnt_sprt);
            last = port_cur_page;
        }
    }
    /* PORT_SHOT_ON_SPRITE=N: capture the first frame drawing > N sprites */
    if (getenv("PORT_SHOT_ON_SPRITE")) { static int done=0;
      int thr = atoi(getenv("PORT_SHOT_ON_SPRITE"));
      if (!done && port_cnt_sprt > thr) { host_screenshot(); done=1; } }
    SDL_GL_SwapWindow(g_win);          /* present; frame was cleared by PutDrawEnv */

    Uint64 freq = SDL_GetPerformanceFrequency();
    if (g_refresh_hz > 0) {            /* cap to target; 0 = uncapped */
        double target = 1.0 / (double)g_refresh_hz;
        double elapsed = (double)(SDL_GetPerformanceCounter() - g_last_present) / freq;
        if (elapsed < target) {
            Uint32 ms = (Uint32)((target - elapsed) * 1000.0);
            if (ms > 33) ms = 33;             /* never oversleep */
            if (ms > 0) SDL_Delay(ms);
        }
    }
    g_last_present = SDL_GetPerformanceCounter();

    /* optional actual-fps readout (PORT_FPS_SHOW=1) */
    if (getenv("PORT_FPS_SHOW")) {
        static Uint64 t0; static int fr;
        if (!t0) t0 = SDL_GetPerformanceCounter();
        if (++fr >= 120) {
            double dt = (double)(SDL_GetPerformanceCounter() - t0) / freq;
            fprintf(stderr, "[fps] %.1f (cap %d)\n", fr/dt, g_refresh_hz);
            fr = 0; t0 = SDL_GetPerformanceCounter();
        }
    }
}
