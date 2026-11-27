/* Sonic the Hedgehog (1991) on the NumWorks calculator: what every part of the game shares.
 *
 * The game keeps the Mega Drive original's own design, so that it plays the same: 128 objects of 64
 * bytes run one after another each frame, with a routine number each; the level is a layout of
 * 256x256 chunks made of 16x16 blocks made of 8x8 patterns, drawn into two 64x32 tile planes as the
 * camera moves; a sprite list and a scroll table go to the video chip (vdp.c) once a frame, which
 * draws the 320x224 picture line by line. Its numbers, tables and orders follow the Sonic Retro
 * disassembly, which gives every variable its name: v_screenposx is V.screenposx here. */
#ifndef S1_H
#define S1_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "data.h"
#include "s1const.h"

/* ------------------------------------------------------------------ data (tools/pack.py) */
extern const uint8_t s1_data[];
#define DATA(off) ((const void *)(s1_data + (off)))
#define DATAW(off) ((const uint16_t *)(const void *)(s1_data + (off)))

/* ------------------------------------------------------------------ fixed point */
/* a 16.16 position: whole pixels in the high half, as the original's long obX */
typedef union {
  int32_t l;
  struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ /* (a big-endian calculator, the fx-CG50: the high half first) */
    int16_t i;
    uint16_t f;
#else
    uint16_t f;
    int16_t i;
#endif
  } w;
} fix_t;

/* ------------------------------------------------------------------ objects */
typedef struct Obj {
  uint8_t id;          /* $00: 0 for a free slot */
  uint8_t render;      /* $01: sprite_* flags */
  uint16_t gfx;        /* $02: first art tile, palette line, priority */
  const uint8_t *map;  /* $04: sprite mappings */
  fix_t x;             /* $08 (for objects fixed on screen, x.w.f is their screen y, obScreenY) */
  fix_t y;             /* $0C */
  int16_t vx, vy;      /* $10, $12: 8.8 pixels a frame */
  int16_t inertia;     /* $14: speed along the ground */
  uint8_t height;      /* $16: half height */
  uint8_t width;       /* $17: half width */
  uint8_t priority;    /* $18: sprite layer, 0 in front */
  uint8_t actwid;      /* $19: half width on screen */
  uint8_t frame;       /* $1A */
  uint8_t aniframe;    /* $1B */
  uint8_t anim;        /* $1C */
  uint8_t prevani;     /* $1D */
  uint8_t timeframe;   /* $1E */
  uint8_t delayani;    /* $1F */
  uint8_t coltype;     /* $20: what touching it does */
  uint8_t colprop;     /* $21 */
  uint8_t status;      /* $22 */
  uint8_t respawn;     /* $23: its entry in the object state list */
  uint8_t routine;     /* $24 */
  uint8_t routine2;    /* $25 */
  uint8_t angle;       /* $26 */
  uint8_t x27;         /* $27 */
  union {              /* $28-$3F: the subtype, then each object's own */
    uint8_t b[24];
    int16_t w[12];
    uint16_t uw[12];
    int32_t l[6];
  } s;
} Obj;

#define obSubtype(o) ((o)->s.b[0])
/* the object's own bytes, words and longs, by the original's offsets ($28-$3F) */
#define OB(o, off) ((o)->s.b[(off) - 0x28])
#define OW(o, off) ((o)->s.w[((off) - 0x28) >> 1])
#define OUW(o, off) ((o)->s.uw[((off) - 0x28) >> 1])
#define OL(o, off) ((o)->s.l[((off) - 0x28) >> 2])
#define obScreenY(o) (*(int16_t *)&(o)->x.w.f)

#define NOBJ 128
#define NRESERVED 32
extern Obj objs[NOBJ];
#define player (&objs[0])
#define OBJ(n) (&objs[n])
/* objects keep others' places as the original kept their RAM addresses */
#define OADDR(p) ((uint16_t)(0xD000 + ((p) - objs) * 0x40))
#define OPTR(a) (&objs[((uint16_t)(a) - 0xD000) >> 6])

typedef void (*ObjFn)(Obj *);
extern const ObjFn obj_index[];

/* ------------------------------------------------------------------ the video chip (vdp.c) */
extern const uint8_t *vram[2048];          /* each tile of video memory: its 32-byte pattern */
extern uint16_t plane_a[32][64], plane_b[32][64];
extern uint16_t cram[64];                  /* the colours shown, Mega Drive words */
typedef struct {
  int16_t y;
  uint8_t size, link;
  uint16_t tile;
  int16_t x;
} Sprite;
extern Sprite spritetable[80];             /* v_spritetablebuffer */
extern uint8_t vdp_backdrop;               /* colour shown where nothing is drawn */
extern int16_t vdp_vscroll_a, vdp_vscroll_b;
extern bool vdp_line_scroll;               /* a scroll per line, or per 8 lines (title) */
extern int vdp_hint_line;                  /* the water palette from this line on (or none: -1) */
extern bool vdp_display;                   /* the picture is on */
void vdp_load_art(int art, int tile);      /* art from data.bin into video memory, at this tile */
void vdp_load_art_part(int art, int first, int count, int tile);
void vdp_clear_planes(void);
void vdp_render(void);
void vdp_fill_plane(uint16_t (*plane)[64], int col, int row, int w, int h, const uint16_t *words, uint16_t add);

/* ------------------------------------------------------------------ data access */
const uint8_t *map_get(int id);                       /* a mappings table */
const uint8_t *map_frame(const uint8_t *map, int f);  /* its frame: a count, then 6-byte pieces */
const uint8_t *ani_get(int id);                       /* an animation table */

/* ------------------------------------------------------------------ the game's variables (RAM) */
typedef struct {
  fix_t x, y;
} Cam;

typedef struct {
  /* game */
  uint8_t gamemode;
  uint8_t jpadhold2, jpadpress2, jpadhold1, jpadpress1;
  uint16_t generictimer;
  int16_t scrposy_vdp, bgscrposy_vdp;
  uint8_t hblank_line;
  uint8_t pfade_start, pfade_size;
  uint8_t vblank_routine;
  uint16_t pcyc_num, pcyc_time;
  uint32_t random;
  uint16_t f_pause;
  uint16_t f_hblank_pal;
  int16_t waterpos1, waterpos2, waterpos3;
  uint8_t f_water, wtr_routine, f_wtr_state, f_doupdatesinhblank;
  uint16_t pal_buffer[0x18];
  /* level */
  Cam cam[4];                    /* v_screenposx/y, v_bgscreenposx/y, v_bg2..., v_bg3... */
  int16_t limitleft1, limitright1, limittop1, limitbtm1;
  int16_t limitleft2, limitright2, limittop2, limitbtm2;
  int16_t limitleft3;
  int16_t scrshiftx, scrshifty;
  int16_t lookshift;
  uint8_t dle_routine;
  uint8_t f_nobgscroll;
  uint8_t fg_xblock, fg_yblock, bg1_xblock, bg1_yblock, bg2_xblock, bg2_yblock, bg3_xblock, bg3_yblock;
  uint8_t scroll_flags[4];       /* v_fg_scroll_flags, bg1, bg2, bg3 (their high bytes) */
  uint8_t f_bgscrollvert;
  int16_t sonspeedmax, sonspeedacc, sonspeeddec;
  uint8_t sonframenum, f_sonframechg;
  uint8_t anglebuffer, anglebuffer2;
  uint8_t opl_routine;
  int16_t opl_screen;
  uint32_t opl_data[4];          /* (offsets into data.bin of the object lists) */
  int16_t ssangle, ssrotate;
  uint16_t btnpushtime1, btnpushtime2;
  int16_t palchgspeed;
  const uint8_t *collindex;
  uint16_t palss_num, palss_time, palss_index;
  uint16_t ssbganim;
  int16_t obj31ypos;
  uint8_t bossstatus;
  uint16_t trackpos;
  uint8_t f_lockscreen;
  uint8_t loop256[4];            /* v_256loop1, v_256loop2, v_256roll1, v_256roll2 */
  uint8_t lani[6][2];            /* level art animations: frame, time */
  uint16_t gfxbigring;
  uint8_t f_conveyrev;
  uint8_t obj63[6];
  uint8_t f_wtunnelmode, f_playerctrl, f_wtunneldisallow, f_slidemode;
  uint8_t obj6B, f_lockctrl, f_bigring, f_obj56;
  uint16_t itembonus, timebonus, ringbonus;
  uint8_t f_endactbonus, sonicend;
  uint16_t lz_deform;
  uint8_t f_switch[16];
  /* screens */
  uint16_t palette_water[64], palette[64], palette_fading[64], palette_water_fading[64];
  uint8_t objstate[0x102];          /* (0xC0 in the original, with the stack after it) */
  /* kept across levels */
  uint16_t f_restart;
  uint16_t framecount;
  uint8_t debugitem;
  uint16_t debuguse;
  uint8_t debugspeedtimer, debugspeed;
  uint32_t vblank_count;
  uint8_t zone, act;
  uint8_t lives;
  uint16_t air;
  uint8_t lastspecial;
  uint8_t continues;
  uint8_t f_timeover;
  uint8_t lifecount, f_lifecount, f_ringcount, f_timecount, f_scorecount;
  uint16_t rings;
  uint8_t timemin, timesec, timecent; /* v_time: (unused), minutes, seconds, frames */
  uint32_t score;
  uint8_t shield, invinc, shoes, unused1;
  uint8_t lastlamp, lastlamp_dup;
  int16_t lamp_xpos, lamp_ypos;
  uint16_t lamp_rings;
  uint8_t lamp_timemin, lamp_timesec, lamp_timecent;
  uint8_t lamp_dle;
  int16_t lamp_limitbtm;
  int16_t lamp_scrx, lamp_scry, lamp_bgscrx, lamp_bgscry, lamp_bg2scrx, lamp_bg2scry, lamp_bg3scrx, lamp_bg3scry;
  int16_t lamp_wtrpos;
  uint8_t lamp_wtrrout, lamp_wtrstat, lamp_lives;
  uint8_t emeralds;
  uint8_t emldlist[6];
  uint16_t oscillate;
  uint16_t osc[0x20];            /* v_oscillate+2: value and speed pairs */
  uint8_t ani_time[4], ani_frame[4];
  uint16_t ani3_buf;
  int16_t limittopdb, limitbtmdb;
  Cam cam_dup[4];
  uint8_t scroll_flags_dup[4];
  uint16_t levselitem;
  uint32_t scorelife;
  uint8_t f_levselcheat, f_slomocheat, f_debugcheat, f_creditscheat;
  uint16_t title_dcount, title_ccount;
  int16_t f_demo;
  uint16_t demonum;
  uint16_t creditsnum;
  uint16_t f_debugmode;
} Vars;
extern Vars V;

/* the background scroll buffer and the scroll table */
extern union bgscroll_u {
  int32_t l[128];
  int16_t w[256];
} bgscroll;
typedef struct {
  int16_t fg, bg;
} HScroll;
extern HScroll hscroll[256];     /* v_hscrolltablebuffer (with room for spills) */

/* sprite display queue: 8 layers of up to 63 objects */
extern struct spritequeue_s {
  uint16_t n;
  Obj *o[63];
} spritequeue[8];

/* the level layout: 8 rows of $40 chunks, foreground and background interleaved as in the original */
extern uint8_t lvllayout[8][2][0x40];
extern const uint16_t *blocks16;    /* v_16x16: 4 tile words a block */
extern const uint16_t *chunks256;   /* v_256x256: 256 block words a chunk */

/* ------------------------------------------------------------------ game modes */
enum {
  id_Sega = 0x00, id_Title = 0x04, id_Demo = 0x08, id_Level = 0x0C, id_Special = 0x10, id_Continue = 0x14,
  id_Ending = 0x18, id_Credits = 0x1C
};
enum {
  id_VBlank_Lag = 0x00, id_VBlank_Sega = 0x02, id_VBlank_Title = 0x04, id_VBlank_Levels = 0x08,
  id_VBlank_SpecialStage = 0x0A, id_VBlank_TitleCards = 0x0C, id_VBlank_Paused = 0x10,
  id_VBlank_PaletteFade = 0x12, id_VBlank_SegaPCM = 0x14, id_VBlank_Continue = 0x16, id_VBlank_Ending = 0x18
};

/* ------------------------------------------------------------------ main.c */
void WaitForVBlank(void);
uint32_t RandomNumber(void);
int16_t CalcSine(uint8_t angle, int16_t *cos);
uint8_t CalcAngle(int16_t x, int16_t y);
void game_quit(void);              /* Home: save and leave, from anywhere */

/* ------------------------------------------------------------------ palettes (palette.c) */
void PalLoad(int id);              /* palid_*: straight to v_palette */
void PalLoad_Fade(int id);         /* to the fade-in buffer */
void PalLoad_Water(int id);
void PalLoad_Fade_Water(int id);
void PaletteFadeIn(void);
void PalFadeIn_Alt(void);
void PaletteFadeOut(void);
void PaletteWhiteIn(void);
void PaletteWhiteOut(void);
void FadeIn_FromBlack(void);
void FadeOut_ToBlack(void);
void WhiteIn_FromWhite(void);
void WhiteOut_ToWhite(void);
void PaletteCycle(void);
void PalCycle_Title(void);
enum {
  palid_SegaBG, palid_Title, palid_LevelSel, palid_Sonic, palid_GHZ, palid_LZ, palid_MZ, palid_SLZ, palid_SYZ,
  palid_SBZ1, palid_Special, palid_LZWater, palid_SBZ3, palid_SBZ3Water, palid_SBZ2, palid_LZSonWater,
  palid_SBZ3SonWat, palid_SSResult, palid_Continue, palid_Ending
};

/* ------------------------------------------------------------------ level (level.c) */
void LevelSizeLoad(void);
void LevelDataLoad(void);
void LevelLayoutLoad(void);
void level_blocks_load(void);
void DrawChunks_BG(void);
void DrawChunks_FG(void);
void LoadTilesFromStart(void);
void LoadTilesAsYouMove(void);
void LoadTilesAsYouMove_BGOnly(void);
void DeformLayers(void);
void ColIndexLoad(void);
void AddPLC(int id);
void NewPLC(int id);
void ClearPLC(void);
void RunPLC(void);
void QuickPLC(int id);
bool plc_pending(void);
void DynamicLevelEvents(void);
void OscillateNumInit(void);
void OscillateNumDo(void);
void SynchroAnimate(void);
void SignpostArtLoad(void);
void AnimateLevelGfx(void);
void LZWaterFeatures(void);
void ClearScreen(void);
enum {
  plcid_Main, plcid_Main2, plcid_Explode, plcid_GameOver, plcid_GHZ, plcid_GHZ2, plcid_LZ, plcid_LZ2, plcid_MZ,
  plcid_MZ2, plcid_SLZ, plcid_SLZ2, plcid_SYZ, plcid_SYZ2, plcid_SBZ, plcid_SBZ2, plcid_TitleCard, plcid_Boss,
  plcid_Signpost, plcid_Warp, plcid_SpecialStage, plcid_GHZAnimals, plcid_LZAnimals, plcid_MZAnimals,
  plcid_SLZAnimals, plcid_SYZAnimals, plcid_SBZAnimals, plcid_SSResult, plcid_Ending, plcid_TryAgain,
  plcid_EggmanSBZ2, plcid_FZBoss
};

/* ------------------------------------------------------------------ objects (obj.c) */
void ExecuteObjects(void);
void BuildSprites(void);
void BuildSpr_Pieces(const uint8_t *p, int count, int16_t x, int16_t y, uint8_t r, uint16_t gfx);
void BuildSpr_End(void);
void DisplaySprite(Obj *o);
void DeleteObject(Obj *o);
Obj *FindFreeObj(void);
Obj *FindNextFreeObj(Obj *parent);
void ObjPosLoad(void);
void AnimateSprite(Obj *o, const uint8_t *ani);
void ObjectFall(Obj *o);
void SpeedToPos(Obj *o);
bool ChkObjectVisible(Obj *o);
bool ChkPartiallyVisible(Obj *o);
void RememberState(Obj *o);
void obj_out_of_range(Obj *o);   /* out_of_range: deletes it when far from the camera */
bool obj_far(Obj *o);
void AddPoints(int points);
#endif
