/* Doom on the calculator: what the platform's parts say to each other. */
#ifndef DOOM_NW_H
#define DOOM_NW_H
#include "doomtype.h"

/* the zone on a computer: the calculator's (about 53 KB), larger as the computer's pointers are 64-bit where
 * the calculator's levels use 16-bit ones (its levels take 1.7 to 1.8 times as much) */
#define NW_HOST_ZONE (96 * 1024)

/* tests: the level to start in (15 for E1M5), or 0 for the title (built with NW_WARP=15, or play --warp) */
extern int nw_warp;

/* i_system.c: Home, On/Off and Quit end the main loop; I_Error and I_Quit jump back to main
 * (__builtin_setjmp and __builtin_longjmp on nw_exit) */
extern boolean nw_quit;
#if (defined(HOST) && defined(__APPLE__)) || defined(NP_PORT) /* (no __builtin_setjmp on arm64) */
#include <setjmp.h>
extern jmp_buf nw_exit;
#define __builtin_setjmp setjmp
#define __builtin_longjmp longjmp
#else
extern void *nw_exit[5];
#endif

/* i_video.c: an error message on the screen, until a key is pressed */
void nw_show_error(const char *msg);

/* i_input.c: Home or On/Off is pressed (the moment is kept, then the app quits) */
extern boolean nw_home;
extern boolean nw_always_run;   /* Options, More: run without holding the run key */
void nw_input_init(void);
/* the calculator's keys (eadk_key_*) */
enum {
    NK_LEFT = 0, NK_UP = 1, NK_DOWN = 2, NK_RIGHT = 3, NK_OK = 4, NK_BACK = 5, NK_HOME = 6, NK_ONOFF = 8,
    NK_SHIFT = 12, NK_ALPHA = 13, NK_XNT = 14, NK_VAR = 15, NK_TOOLBOX = 16, NK_BACKSPACE = 17, NK_EXP = 18,
    NK_LN = 19, NK_LOG = 20, NK_IMAG = 21, NK_COMMA = 22, NK_POWER = 23, NK_SIN = 24, NK_COS = 25, NK_TAN = 26,
    NK_PI = 27, NK_SQRT = 28, NK_SQUARE = 29, NK_SEVEN = 30, NK_EIGHT = 31, NK_NINE = 32, NK_LPAREN = 33,
    NK_RPAREN = 34, NK_FOUR = 36, NK_FIVE = 37, NK_SIX = 38, NK_MUL = 39, NK_DIV = 40, NK_ONE = 42, NK_TWO = 43,
    NK_THREE = 44, NK_PLUS = 45, NK_MINUS = 46, NK_ZERO = 48, NK_DOT = 49, NK_EE = 50, NK_ANS = 51, NK_EXE = 52,
    NK_COUNT = 53
};
/* what a key can do: Doom's actions, each the key of Doom's keyboard that does it */
enum {
    NA_NONE, NA_FORWARD, NA_BACKWARD, NA_TURNLEFT, NA_TURNRIGHT, NA_FIRE, NA_USE, NA_STRAFE, NA_RUN,
    NA_STRAFELEFT, NA_STRAFERIGHT, NA_WEAPON1, NA_WEAPON2, NA_WEAPON3, NA_WEAPON4, NA_WEAPON5, NA_WEAPON6,
    NA_WEAPON7, NA_AUTOMAP, NA_MENU, NA_PAUSE, NA_BIGGER, NA_SMALLER, NA_FOLLOW, NA_GRID, NA_WHOLEMAP, NA_MARK,
    NA_CLEARMARKS, NA_GAMMA, NA_QUICKSAVE, NA_QUICKLOAD, NA_MESSAGES, NA_DETAIL, NA_CHEAT, NA_NEXTWEAPON,
    NA_PREVWEAPON, NA_COUNT
};
typedef struct {
    const char *name;
    int key;
} nw_action_t;
extern const nw_action_t nw_actions[NA_COUNT];
extern uint8_t nw_binding[NK_COUNT]; /* each key's action (kept in doom.cfg) */
void nw_bindings_default(void);
const char *nw_key_name(int k);
boolean nw_key_bindable(int k);
const char *nw_keys_of(int action, char *buf, int size);
extern int nw_capture;              /* an action the next key pressed is given (the options), or -1 */
extern boolean nw_typing;           /* a cheat is being typed */
void nw_cheat_start(void);

/* saves.c: the save slots (doomsav0.dsg to doomsav5.dsg), the moment kept on Home (slot NW_RESUME_SLOT,
 * read once on the next start) and the copy of them all in doom_saves.py */
#define NW_RESUME_SLOT 6
#define NW_SLOTS 7
void nw_saves_restore(void);       /* from doom_saves.py, when the files themselves are gone */
boolean nw_resume_ready(void);
void nw_resume_save(void);          /* the game as it is now, if there is one to keep */
void nw_resume_done(void);          /* the moment was read back: it goes */
void nw_settings_load(void);
void M_NwViewLoaded(void); /* m_menu.c: the loaded screen size and detail take effect */
void nw_settings_save(void);
extern boolean nw_first_start;     /* no settings yet: the first game starts with a word on the keys */
#endif
