/* frank-blood: no Sound Blaster here; AL_MIDI only asks for its OPL type. */
#ifndef APOGEE_OPL_BLASTER_H
#define APOGEE_OPL_BLASTER_H
enum { BLASTER_Ok = 0, BLASTER_Error = -1 };
enum { SB = 1, SBPro = 2, SB20 = 3, SBPro2 = 4, SB16 = 6 };
typedef struct { unsigned Address, Type, Interrupt, Dma8, Dma16, Midi, Emu; } BLASTER_CONFIG;
static inline int BLASTER_GetCardSettings(BLASTER_CONFIG *c) { (void)c; return BLASTER_Error; }
static inline int BLASTER_GetEnv(BLASTER_CONFIG *c) { (void)c; return BLASTER_Error; }
#endif
