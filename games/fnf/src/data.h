/* The game's charts and pictures, as tools/data.py packs them from the Funkin' Crew's files (data.c and ids.h are
 * made at build time, in the build folder: they are not in this repository). */
#ifndef FNF_DATA_H
#define FNF_DATA_H
#include <stdint.h>
#include "ids.h"

#define FNF_NONE 0xFFFF

/* A picture: 4 bits a pixel (0 transparent, unless it's a stage's background), rows of (w + 1) / 2 bytes,
 * DEFLATE-packed at fnf_blob + at; (x, y): its top left from its anchor (a character's feet, a note's middle). */
typedef struct {
  uint16_t w, h;
  int16_t x, y;
  uint16_t pal, flags;
  uint32_t at, size;
} fnf_sprite_t;

/* An animation: n frames (sprites sprite.. sprite + n - 1), frame i shown from the original's frame at[i]
 * (of len, at fps). */
enum { FNF_AF_LOOP = 1, FNF_AF_SING = 2, FNF_AF_MISS = 4, FNF_AF_DANCE = 8 };
typedef struct {
  uint16_t sprite;
  uint8_t n, len, fps, flags;
  uint8_t at[6];
} fnf_anim_t;

enum { FNF_SLOT_DANCE_L, FNF_SLOT_DANCE_R, FNF_SLOT_SING, FNF_SLOT_MISS = FNF_SLOT_SING + 4, FNF_SLOTS = FNF_SLOT_MISS + 4 };
enum { FNF_CHAR_ALTERNATE = 1, FNF_CHAR_PIXEL = 2, FNF_CHAR_BF = 4, FNF_CHAR_PICO = 8, FNF_CHAR_ICON_FLIP = 16 };
/* A character: its animations fnf_anim[anim .. anim + count - 1] (their names: fnf_anim_name), which of them dance,
 * sing and miss (slot: an animation of its own, 255 none); it dances every `dance` steps and stops singing after
 * `sing` steps. death: a player's game over (fnf_death, 255 none); top: its hitbox's top left from its feet. */
typedef struct {
  uint16_t anim;
  uint8_t count, dance, sing, flags, icon;
  uint8_t slot[FNF_SLOTS];
  uint8_t death;
  uint16_t frame_max;   /* its biggest frame, unpacked (bytes) */
  int16_t top[2];
} fnf_char_t;

/* A game over animation: n frames (the original's every step-th, of len, at fps) in a w x h box at (x, y) from the
 * character's feet. Frame 0 is whole, each next one XORed with the one before; each DEFLATE-packed, one after the
 * other from fnf_blob + at, their sizes from fnf_death_size[frame] (0: the same as the one before). */
typedef struct {
  int16_t x, y;
  uint16_t w, h, pal;
  uint8_t n, step, len, fps;
  uint16_t frame;
  uint32_t at;
} fnf_death_anim_t;

/* A player's game over (GameOverSubState): firstDeath, deathLoop and deathConfirm (fnf_death_anim[anim..], FNF_NONE:
 * none), the camera ending on cam from its feet; firstDeath gives way to deathLoop at its frame loop_at (0: at its
 * end); after a retry the screen fades `fade` ms later; knife: Nene's knife toss (FNF_NONE: none), from Girlfriend's
 * top left + knife_at. */
enum { FNF_DF_PIXEL = 1 };   /* the fades go in ten steps */
typedef struct {
  uint16_t anim, knife;
  int16_t cam[2], knife_at[2];
  uint16_t fade;
  uint8_t loop_at, flags;
} fnf_death_t;

/* A note kind a character sings its own way: the animation for each direction (255: the usual one, 254: none). */
typedef struct {
  uint8_t chr, kind;
  uint8_t anim[4];
} fnf_kind_t;

/* A stage: its background (320x240), the pictures in front of the characters (after the first, second and third of
 * them in `order`, FNF_NONE none), where the player's, the opponent's and Girlfriend's feet are on the screen (and
 * LE SSERAFIM's other members'), and its scale (world units to pixels, x 10000). */
typedef struct {
  uint16_t bg, front[3];
  int16_t feet[3][2], extra[3][2];
  uint8_t order[3];
  uint16_t scale;
} fnf_stage_t;

/* A change of tempo: from time t (us), a step lasts step / 256 us; steps0: the steps before it (16.16). */
typedef struct {
  int32_t t;
  uint32_t step;
  int32_t steps0;
} fnf_tempo_t;

/* A song event (FNF_EV_...). */
typedef struct {
  int32_t t;
  uint8_t type, a, b, c;
  int32_t v, d;
} fnf_event_t;

/* A chart: one difficulty of a variation; its notes DEFLATE-packed (raw bytes once unpacked): per note, LEB128
 * numbers: the time since the last note (us), lane (0-3 the player's, 4-7 the opponent's) | hold << 3 | kind << 4,
 * and for a hold its length (us). total: the player's notes that count; speed: scroll speed x 1000. */
typedef struct {
  uint16_t var;
  uint8_t diff, pad;
  uint32_t at;
  uint16_t size, raw, notes, total, speed;
} fnf_chart_t;

enum { FNF_DIFF_EASY, FNF_DIFF_NORMAL, FNF_DIFF_HARD, FNF_DIFF_ERECT, FNF_DIFF_NIGHTMARE, FNF_DIFFS };
enum { FNF_VAR_DEFAULT, FNF_VAR_ERECT, FNF_VAR_PICO, FNF_VAR_BF };
/* A variation of a song: its stage, note style (0 Funkin', 1 pixel), characters (player, opponent, Girlfriend;
 * 255 none), tempo, events, charts and length (ms). extra: LE SSERAFIM's other three members; flags:
 * FNF_VF_OPPONENT_AT_GF (Girlfriend sings against you, from her place: the Tutorial), FNF_VF_SOLO (only your arrows,
 * in the middle: Blazin', as its script does). */
enum { FNF_VF_OPPONENT_AT_GF = 1, FNF_VF_SOLO = 2 };
typedef struct {
  uint8_t song, kind, stage, style;
  uint8_t chr[3], extra;
  uint16_t tempo, ntempo, event, nevent, chart;
  uint8_t ncharts, flags;
  uint32_t length;
} fnf_var_t;

typedef struct {
  const char *name;
  uint8_t level, var, nvar, icon;
} fnf_song_t;

typedef struct {
  const char *name;
  uint8_t song[4], n;
} fnf_level_t;

/* A letter of the menus' bold font: its sprite (FNF_NONE: none) and how far it moves the pen. */
typedef struct {
  uint16_t sprite, advance;
} fnf_glyph_t;

extern const uint16_t fnf_palette[FNF_PALETTES][16];
extern const fnf_sprite_t fnf_sprite[FNF_SPRITES];
extern const fnf_anim_t fnf_anim[FNF_ANIMS];
extern const uint8_t fnf_anim_name[FNF_ANIMS];
extern const fnf_char_t fnf_char[FNF_CHARS];
extern const fnf_death_t fnf_death[FNF_DEATHS];
extern const fnf_death_anim_t fnf_death_anim[FNF_DEATH_ANIMS];
extern const uint16_t fnf_death_size[FNF_DEATH_FRAMES];
extern const fnf_kind_t fnf_kind[FNF_KINDS];
extern const uint16_t fnf_icon[FNF_ICONS][3];
extern const fnf_stage_t fnf_stage[FNF_STAGES];
extern const fnf_tempo_t fnf_tempo[FNF_TEMPOS];
extern const fnf_event_t fnf_event[FNF_EVENTS];
extern const fnf_chart_t fnf_chart[FNF_CHARTS];
extern const fnf_var_t fnf_var[FNF_VARS];
extern const fnf_song_t fnf_song[FNF_SONGS];
extern const fnf_level_t fnf_level[FNF_LEVELS];
extern const fnf_glyph_t fnf_bold[95];
extern const uint8_t fnf_blob[FNF_BLOB];
#endif
