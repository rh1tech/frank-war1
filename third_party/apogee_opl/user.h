/* frank-blood: no command line for AL_MIDI. */
#ifndef APOGEE_OPL_USER_H
#define APOGEE_OPL_USER_H
#define NO_ADLIB_DETECTION "NOADLIBDETECTION"
static inline int USER_CheckParameter(const char *p) { (void)p; return 0; }
#endif
