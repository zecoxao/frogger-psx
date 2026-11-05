/* port_stubs.c -- link-completing stubs for the PSX runtime surface not yet
 * implemented. NO PsyQ headers are included on purpose: K&R-style empty
 * definitions match any call ABI by symbol name, avoiding signature conflicts
 * with the prototypes the game sees. Everything here is a milestone target:
 *   - gte register ops  -> software GTE (next)
 *   - MR asm / mesh-poly emitters -> raster milestone
 *   - Cd, Spu, Ss, DecDCT, card, TAP : I/O + audio + FMV subsystems
 * Note: open/read/write/close/lseek/creat are intentionally NOT defined here --
 * they resolve to host libc (defining them would hijack SDL/GL file access).
 */

/* ---- GTE register ops are implemented for real in port_gte.c ---- */
/* MRDisplayMeshPolys_* are now the real C emitters (MR_P_*.C). */
/* ---- MR asm routines still stubbed (quaternion / map render / depack) ---- */
void MRQuaternionToMatrixASM(){}  void MRQuaternionBToMatrixASM(){}
void MRInterpolateQuaternionsASM(){} void MRInterpolateQuaternionsBToMatrixASM(){}
void MapRenderTrisASM(){} void MapRenderQuadsASM(){}
void MRPPDecrunchBuffer(){}

/* ---- libcd is implemented for real in port_cd.c (reads FROGPSX.MWD) ---- */

/* ---- libspu/libsnd are implemented for real in port_audio.c (SDL audio) ---- */

/* ---- libpress (FMV decode) ---- */
void DecDCTReset(){} void DecDCTin(){} long DecDCTout(){ return 0; }
void *DecDCToutCallback(){ return 0; }
void DecDCTvlc2(){} void DecDCTvlcBuild(){}

/* ---- CD streaming (libds) ---- */
int StCdIntrFlag = 0;
void StCdInterrupt(){} void StSetStream(){} long StGetNext(){ return 0; }
void StSetRing(){} void StUnSetRing(){} void StFreeRing(){}

/* ---- memcard / card BIOS (open/read/write/close come from libc) ---- */
void InitCARD(){} long StartCARD(){ return 1; }
void _bu_init(){} long _card_clear(){ return 1; } long _card_info(){ return 1; } long _card_load(){ return 1; }
long firstfile(){ return 0; } long nextfile(){ return 0; }
long erase(){ return 0; } long format(){ return 1; }

/* ---- controller tap: InitTAP/StartTAP are implemented in port_runtime.c ---- */

/* ---- kernel / BIOS ---- */
long OpenEvent(){ return 0; } long EnableEvent(){ return 1; } long TestEvent(){ return 1; }
int  EnterCriticalSection(){ return 0; } void ExitCriticalSection(){}
void FlushCache(){} unsigned long SetSp(){ return 0; }
void ChangeClearPAD(){}

/* ---- SN host file IO (dev tool; unused on real hardware paths) ---- */
long PCopen(){ return -1; } long PCcreat(){ return -1; } long PCclose(){ return 0; }
long PClseek(){ return 0; } long PCread(){ return 0; } long PCwrite(){ return 0; }

/* ---- linker-script symbols (addresses/sizes the PSX startup provided) ----
 * __heapbase/__heapsize are provided (as a real malloc'd heap) in
 * port_runtime.c, since they must be a valid pointer + size for MRInitMem. */
unsigned long __bsslen, __datalen, __textlen;
unsigned long saved_stack, stack_safety;
