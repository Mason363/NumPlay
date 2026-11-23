/* The app's name on the calculator's home screen. The Makefile builds NumPlay
 * under a few names and icons (VARIANTS); everything else is the same. */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = NP_APP_NAME;

/* How NumPlay starts until its Settings are changed (launcher/src/storage.c): 'D', version 1,
 * then numplay.set's flags (1: as Matrices, 2: no hint) and its secret. NumPlay Studio, the
 * customizer website, writes its own with the name. */
#ifndef NP_START_FLAGS
#define NP_START_FLAGS 0
#endif
#ifndef NP_START_SECRET
#define NP_START_SECRET 0
#endif
#ifndef NP_START_THEME
#define NP_START_THEME 0
#endif
const unsigned char np_defaults[5] __attribute__((section(".rodata.np_defaults"))) = {'D', 1, NP_START_FLAGS, NP_START_SECRET, NP_START_THEME};
