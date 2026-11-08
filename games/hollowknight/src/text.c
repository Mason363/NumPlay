/* Text: the game's sheets' entries (TEXT, in the pack's own code page) in its fonts (FONT: glyphs of 4-bit alpha);
 * the dialogue box (DialogueManager: Box Open, its Text's DialogueBox and Dialogue Page Control) and the prompt
 * markers over what the Knight can act on (PromptMarker). The boxes are a third bigger than the game's, the text read
 * in them bigger still, to read on the calculator's screen (tools/text.py: the titles as the game has them). */
#include <math.h>
#include "game.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))   /* (the HUD's pixels a unit) */

const uint8_t *text_get(int id) {
  const uint8_t *s = section(SEC_TEXT);
  if ((uint32_t)id >= rd32(s)) return (const uint8_t *)"";
  return s + 4 + 4 * rd32(s) + rd32(s + 4 + 4 * id);
}

const uint8_t *font_style(int style) {
  const uint8_t *f = section(SEC_FONT);
  return f + rd32(f + 4 + 4 * style);
}

static float adv_of(const uint8_t *st, uint8_t c) {
  const Glyph *g = font_glyph(st, c, 0);
  return g ? g->adv * (1.0f / 64) : 0;
}

float text_width(int style, const uint8_t *s, int n) {
  const uint8_t *st = font_style(style);
  float w = 0;
  for (int i = 0; i < n && s[i]; i++) w += adv_of(st, s[i]);
  return w;
}

/* a line from s[i] in width (word wrapped as TextMeshPro does): its end; *next where the next line starts */
static int line_end(const uint8_t *st, const uint8_t *s, int i, float width, int *next) {
  float w = 0;
  int space = -1;
  for (int j = i;; j++) {
    uint8_t c = s[j];
    if (!c || c == TEXT_PAGE) return *next = j, j;
    if (c == TEXT_BR) return *next = j + 1, j;
    float a = adv_of(st, c);
    if (c != ' ' && w + a > width && j > i) {
      if (space > i) return *next = space + 1, space;
      return *next = j, j;
    }
    if (c == ' ') space = j;
    w += a;
  }
}

/* a text in a box (HUD units: its top at y, w wide), word wrapped: each line from x (align 0), centered on it (1) or
 * ending at it (2) -> how far down it goes (HUD units) */
float text_box(int text, int style, float x, float y, float w, int align, float a) {
  if (text < 0) return 0;
  const uint8_t *s = text_get(text), *st = font_style(style);
  float px = HUD_PX, wp = w * px;
  float py = VIEW_H / 2 - y * px + font_asc(st);
  uint32_t col = (uint32_t)(a * 255 + 0.5f) << 24 | 0xFFFFFF;
  int lines = 0;
  for (int i = 0; s[i];) {
    int next, e = line_end(st, s, i, wp, &next);
    if (e > i && s[i] != TEXT_PAGE) {
      float lw = text_width(style, s + i, e - i);
      float lx = VIEW_W / 2 + x * px - (align == 1 ? lw / 2 : align == 2 ? lw : 0);
      if (a > 0) gfx_text(style, lx, py, s + i, e - i, col, 0);
    }
    lines++;
    py += font_line(st);
    if (next <= i) break;
    i = next;
  }
  return lines * font_line(st) / px;
}

/* a description's style: the message's, else the smaller ones, the first in which it fits in room (HUD units) down
 * from its top, w wide (the last, where none does) */
#ifdef HOST
uint32_t g_text_overrun;   /* (tests: a text too long for its box even in the smallest) */
#endif
int text_fit(int text, float w, float room) {
  static const int8_t styles[] = {STYLE_MSG, STYLE_MSG_S, STYLE_MSG_XS};
  for (unsigned i = 0; i < sizeof styles; i++)
    if (text_box(text, styles[i], 0, 0, w, 0, 0) <= room + 0.01f) return styles[i];
#ifdef HOST
  g_text_overrun++;
  if (getenv("HKOVERRUN")) printf("   text %d: %.2f high in MSG_XS, room %.2f\n", text, text_box(text, STYLE_MSG_XS, 0, 0, w, 0, 0), room);
#endif
  return STYLE_MSG_XS;
}

/* ---------------------------------------------------------------- the dialogue box */
#define BOX_X 0.0f
#define BOX_Y 4.51f          /* (DialogueBox's place: the box scales about it) */
#define TEXT_X (-0.1031f)    /* (Text: its rect's center, its size) */
#define TEXT_Y 4.49f
#define TEXT_W 17.9475f
#define TEXT_H 4.1109f
#define REVEAL_SPEED 65.0f   /* (DialogueBox.revealSpeed; sped up: 200) */
#define MAX_PAGES 24
#define KEYS_CONTINUE (K_JUMP | K_ATTACK | K_SPELL | K_FOCUS | K_DASH | K_OK | K_BACK | K_PAUSE | K_MAP | K_INV)

#define MARKER_Y 1.695f      /* (the arrow's and the stop's, as Dialogue Page Control has them: the box's) */
#define YN_BOX_Y 1.94f       /* (Box Open YN: the box lower) */
#define YN_TEXT_Y 1.89f      /* (Text YN: its rect's center, its height; its lines centred) */
#define YN_TEXT_H 3.9055f

enum { PC_IDLE, PC_PAGE_END, PC_STOP_PAUSE, PC_CONV_END, PC_SFX, PC_END, PC_YN_PAUSE, PC_YN_LIST, PC_YN_CHOSEN };
static struct {
  /* Box Open: the box's scale (iTweenScaleTo) and its FadeGroup (0.5 s up, 0.3 s down, to 0.6); its place (Box Open
   * YN's lower, the HUD left as it is) and its text's */
  bool up, yn_box;
  float box_y, text_y, text_h;
  float scale, scale_from, scale_to, scale_t, scale_time;
  float alpha, fade_t;
  Anim fleur_top, fleur_bot;
  bool fleurs_on;
  /* DialogueBox */
  const uint8_t *text;
  uint16_t page_start[MAX_PAGES + 1];
  uint8_t npages, page;
  int visible;          /* maxVisibleCharacters */
  float reveal, speed;
  bool typing, fast;
  /* Dialogue Page Control, the arrow and the stop marker (Arrow Anim) */
  uint8_t pc;
  Anim arrow, stop;
  bool arrow_on, stop_on;
  bool finished;        /* CONVO_FINISH, since dialogue_start */
  uint32_t prev_keys;
  /* (where a script put the text, the stop, the arrow (SetPosition, HUD units: a lore tablet's); its Stop Pause) */
  float place[3];
  bool placed;
  float stop_pause, pause_t;
  /* Box Open Dream: the dream box (up, or going down a while), the text centred (SetTextMeshProAlignment) */
  Anim dream;
  bool dream_on, centre;
  float dream_off;
  /* Text YN: its Dialogue Page Control (the toll shown, its UI List of Yes and No, TakeGeo), who hears YES or NO */
  bool yn, geo_on, cancel;
  uint8_t yn_cur;       /* (ui_list's Current Item: 1 Yes, 2 No) */
  int16_t toll, requester;
  float yn_t, rep;
  Anim coin;
} dl;

static float ease_sine(float t, bool out) { return out ? sinf(t * 1.5707964f) : 1 - cosf(t * 1.5707964f); }

static void box_scale_to(float to, float time) {
  dl.scale_from = dl.scale, dl.scale_to = to, dl.scale_t = 0, dl.scale_time = time;
}

static void box_up(bool yn) {
  dl.up = true, dl.yn_box = yn;
  dl.box_y = yn ? YN_BOX_Y : BOX_Y;
  if (!yn) hud_slide(true);
  box_scale_to(1, 0.5f);
  dl.fade_t = 0;
  anim_play_from_frame(&dl.fleur_top, CLIP_DIALOGUE_FLEUR_TOP_UP, 0);
  anim_play_from_frame(&dl.fleur_bot, CLIP_DIALOGUE_FLEUR_BOT_UP, 0);
  dl.fleurs_on = true;
}

void dialogue_box_up(void) { box_up(false); }      /* (Box Up) */
void dialogue_box_up_yn(void) { box_up(true); }    /* (Box Open YN's Box Up) */

void dialogue_box_down(void) {
  if (!dl.up) return;
  dl.up = false;
  if (!dl.yn_box) hud_slide(false);
  box_scale_to(0.75f, 0.3f);
  dl.fade_t = 0;
  anim_play_from_frame(&dl.fleur_top, CLIP_DIALOGUE_FLEUR_TOP_DOWN, 0);
  anim_play_from_frame(&dl.fleur_bot, CLIP_DIALOGUE_FLEUR_BOT_DOWN, 0);
}

void dialogue_dream_box(bool up) {
  if (up) {
    /* Box Up: Dream Up, the HUD out */
    anim_play_from_frame(&dl.dream, CLIP_DIALOGUE_DREAM_UP, 0);
    hud_slide(true);
    dl.dream_on = true, dl.dream_off = 0;
  } else if (dl.dream_on) {
    /* Box Down: Dream Down, the HUD in; a quarter second, then gone (Stop Audio) */
    anim_play_from_frame(&dl.dream, CLIP_DIALOGUE_DREAM_DOWN, 0);
    hud_slide(false);
    dl.dream_off = 0.25f;
  }
}

void dialogue_centre(bool on) { dl.centre = on; }

void dialogue_place(int which, float y) {
  if (which < 0 || which > 2) return;
  dl.place[which] = y;
  dl.placed = dl.place[0] != TEXT_Y || dl.place[1] != MARKER_Y || dl.place[2] != MARKER_Y;
}
void dialogue_stop_pause(float t) { dl.stop_pause = t; }

/* the text's rect's middle (HUD units): the box's, the text a third bigger about the box's place; where a script
 * put it, by as much */
static float text_mid(void) {
  float by = dl.yn ? YN_BOX_Y : BOX_Y;
  return dl.yn ? by + (dl.text_y - by) * TEXT_K : dl.text_y + (TEXT_Y - BOX_Y) * (TEXT_K - 1);
}

/* the arrow's and the stop's: as the box has them, about its place; where a script put them, there */
static float marker_y(float y) { return dl.placed ? y : dl.box_y + (y - dl.box_y) * TEXT_K; }

static int lines_per_page(const uint8_t *st) {
  float h = dl.text_h * HUD_PX * TEXT_K;
  return 1 + (int)((h - font_asc(st) - font_desc(st)) / font_line(st));
}

/* SetConversation: its pages (TextMeshPro's page mode: as many lines as the rect holds, and <page>) */
static void set_conversation(const uint8_t *s) {
  const uint8_t *st = font_style(STYLE_DIALOGUE);
  float width = TEXT_W * HUD_PX * TEXT_K;
  int per = lines_per_page(st);
  dl.text = s, dl.npages = 0;
  int i = 0;
  while (s[i] && dl.npages < MAX_PAGES) {
    dl.page_start[dl.npages++] = (uint16_t)i;
    for (int l = 0; l < per && s[i] && s[i] != TEXT_PAGE; l++) line_end(st, s, i, width, &i);
    if (s[i] == TEXT_PAGE) i++;
  }
  if (!dl.npages) dl.page_start[dl.npages++] = 0;
  dl.page_start[dl.npages] = (uint16_t)i;
}

static int page_last(void) {   /* (the page's end: its last character's index + 1) */
  int e = dl.page_start[dl.page + 1];
  while (e > dl.page_start[dl.page] && (dl.text[e - 1] == TEXT_PAGE)) e--;
  return e;
}

static void show_page(int p) {
  if (p >= dl.npages) {
    dl.pc = PC_STOP_PAUSE;   /* (CONVERSATION_END) */
    return;
  }
  dl.page = (uint8_t)p;
  dl.visible = dl.page_start[p];
  dl.typing = true, dl.fast = false, dl.speed = REVEAL_SPEED, dl.reveal = 1;   /* (the first at once) */
}

void dialogue_start(int text) {
  /* StartConversation */
  dl.yn = false, dl.text_y = dl.place[0], dl.text_h = TEXT_H, dl.pause_t = 0;
  set_conversation(text_get(text));
  dl.finished = false;
  dl.pc = PC_IDLE;
  dl.prev_keys = g_hero.keys;
  show_page(0);
}

/* (Text YN's StartConversation: its question, then Yes and No) */
void dialogue_start_yn(int text) {
  dl.yn = true, dl.text_y = YN_TEXT_Y, dl.text_h = YN_TEXT_H;
  set_conversation(text_get(text));
  dl.finished = false, dl.geo_on = false;
  dl.pc = PC_IDLE;
  dl.prev_keys = g_hero.keys;
  show_page(0);
}
void dialogue_yn_toll(int cost) { dl.toll = (int16_t)cost; }
void dialogue_yn_requester(int obj) { dl.requester = (int16_t)obj; }

bool dialogue_finished(void) { return dl.finished; }
bool dialogue_last_page(void) { return !dl.text || dl.page + 1 >= dl.npages; }
bool dialogue_box_shown(void) { return dl.up || dl.alpha > 0; }

static void end_conversation(void) {
  /* End Conversation: the markers down, the text hidden, CONVO_FINISH */
  if (dl.stop_on) anim_play_from_frame(&dl.stop, CLIP_DIALOGUE_STOP_DOWN, 0);
  if (dl.arrow_on) anim_play_from_frame(&dl.arrow, CLIP_DIALOGUE_ARROW_DOWN, 0);
  dl.text = NULL;
  dl.finished = true;
  dl.pc = PC_IDLE;
  vm_broadcast(VMEV_CONVO_FINISH);
}

void dialogue_cancel(void) {
  /* (CONVO CANCEL, LEAVING SCENE) */
  if (dl.text) end_conversation();
}

void dialogue_tick(void) {
  uint32_t keys = g_hero.keys, pressed = keys & ~dl.prev_keys;
  dl.prev_keys = keys;
  bool cont = (pressed & KEYS_CONTINUE) != 0;   /* (ListenForPromptContinue) */
  /* the box */
  if (dl.scale_t < dl.scale_time) {
    dl.scale_t += DT;
    float t = dl.scale_t >= dl.scale_time ? 1 : dl.scale_t / dl.scale_time;
    dl.scale = dl.scale_from + (dl.scale_to - dl.scale_from) * ease_sine(t, dl.scale_to > dl.scale_from);
  }
  dl.fade_t += DT;
  if (dl.up) dl.alpha = fminf(1, dl.fade_t / 0.5f);
  else if (dl.alpha > 0) dl.alpha = fmaxf(0, 1 - dl.fade_t / 0.3f);
  if (dl.fleurs_on) {
    dl.fleur_top.events = dl.fleur_bot.events = 0;
    anim_update(&dl.fleur_top, DT);
    anim_update(&dl.fleur_bot, DT);
    if (!dl.up && !dl.fleur_top.playing && !dl.fleur_bot.playing) dl.fleurs_on = false;
  }
  if (dl.dream_on) {
    dl.dream.events = 0;
    anim_update(&dl.dream, DT);
    if (dl.dream_off > 0 && (dl.dream_off -= DT) <= 0) dl.dream_on = false;
  }
  if (dl.geo_on) anim_update(&dl.coin, DT);
  if (dl.arrow_on) anim_update(&dl.arrow, DT);
  if (dl.stop_on) anim_update(&dl.stop, DT);
  if (!dl.text) return;
  /* DialogueBox's typewriter */
  if (dl.typing) {
    int last = page_last();
    dl.reveal += DT * dl.speed;
    while (dl.reveal >= 1 && dl.visible < last) dl.visible++, dl.reveal -= 1;
    if (dl.visible >= last) {
      dl.typing = false;
      if (dl.page + 1 >= dl.npages && dl.yn) {
        /* (CONVERSATION_END: Activate Geo Text?, a toll's, then a pause; Conversation End: the UI List) */
        dl.geo_on = dl.toll > 0, dl.yn_t = 0, dl.yn_cur = 1, dl.rep = 0;
        dl.pc = dl.geo_on ? PC_YN_PAUSE : PC_YN_LIST;
        if (dl.geo_on) anim_play(&dl.coin, CLIP_HUD_COIN_IDLE);
      } else if (dl.page + 1 >= dl.npages) dl.pc = PC_STOP_PAUSE;   /* CONVERSATION_END */
      else {
        /* PAGE_END: the arrow up */
        dl.pc = PC_PAGE_END;
        dl.arrow_on = true;
        anim_play_from_frame(&dl.arrow, CLIP_DIALOGUE_ARROW_UP, 0);
        return;
      }
    }
  }
  switch (dl.pc) {
    case PC_IDLE:
      if (cont && dl.typing && !dl.fast) dl.fast = true, dl.speed = 200;   /* (SpeedupTypewriter) */
      break;
    case PC_PAGE_END:
      if (cont) {
        /* Show Next Page */
        anim_play_from_frame(&dl.arrow, CLIP_DIALOGUE_ARROW_DOWN, 0);
        dl.pc = PC_IDLE;
        show_page(dl.page + 1);
      }
      break;
    case PC_STOP_PAUSE:
      /* (Stop Pause, then the stop marker up) */
      if ((dl.pause_t += DT) < dl.stop_pause) break;
      dl.pause_t = 0;
      dl.stop_on = true, dl.arrow_on = false;
      anim_play_from_frame(&dl.stop, CLIP_DIALOGUE_STOP_UP, 0);
      dl.pc = PC_CONV_END;
      break;
    case PC_CONV_END:
      if (cont) dl.pc = PC_SFX;   /* (the text cleared, the next frame) */
      break;
    case PC_SFX:
      dl.visible = dl.page_start[dl.page];
      dl.pc = PC_END;
      break;
    case PC_END:
      end_conversation();
      break;
    case PC_YN_PAUSE:
      if ((dl.yn_t += DT) >= 0.2f) dl.pc = PC_YN_LIST, dl.yn_t = 0;
      break;
    case PC_YN_LIST: {
      /* ui_list: left and right (again after 0.25 s held, then each 0.15 s), round; ui_list_button_listen (after a
       * quarter second): submit, or cancel (No) */
      dl.yn_t += DT;
      uint32_t lr = keys & (K_LEFT | K_RIGHT);
      if (!lr) dl.rep = 0;
      else if (pressed & lr) dl.rep = 0.25f, dl.yn_cur = (uint8_t)(3 - dl.yn_cur);
      else if ((dl.rep -= DT) <= 0) dl.rep = 0.15f, dl.yn_cur = (uint8_t)(3 - dl.yn_cur);
      if (dl.yn_t < 0.25f) break;
      if (pressed & K_OK) dl.cancel = false, dl.pc = PC_YN_CHOSEN, dl.yn_t = 0;
      else if (pressed & K_BACK) dl.cancel = true, dl.yn_cur = 2, dl.pc = PC_YN_CHOSEN, dl.yn_t = 0;
      break;
    }
    case PC_YN_CHOSEN: {
      /* (Selection Made: half a second, a cancel's 0.2; Yes without the geo for it: Will Cancel, back) */
      bool short_of = dl.yn_cur == 1 && dl.toll > g_pd.geo;
      if ((dl.yn_t += DT) < (dl.cancel ? 0.2f : 0.5f)) break;
      if (short_of) {
        dl.pc = PC_YN_LIST, dl.yn_t = 0.25f;
        break;
      }
      /* (End Conversation: the text, the list, the toll hidden; Take Geo; YES or NO to who asked) */
      bool yes = dl.yn_cur == 1;
      dl.text = NULL, dl.geo_on = false, dl.yn = false, dl.pc = PC_IDLE;
      if (yes && dl.toll > 0) g_pd.geo -= dl.toll;
      vm_send(dl.requester, yes ? VMEV_YES : VMEV_NO);
      break;
    }
  }
}

#define MARKER_K (1.3f * TEXT_K)   /* (the arrow's and the stop's scale) */
static void marker(int sprite, float x, float y, uint8_t tint) {
  Inst in;
  sprite_inst(sprite, BOX_X + (x - BOX_X) * TEXT_K, marker_y(y), 0, MARKER_K, MARKER_K, tint, &in);
  gfx_hud(&in, 0);
}

static void box_sprite(int sprite, float x, float y, float sx, float sy, uint8_t tint) {
  /* (placed about the box's place at its scale and the text's; sized at its scale) */
  float k = dl.scale * TEXT_K;
  Inst in;
  sprite_inst(sprite, BOX_X + (x - BOX_X) * k, dl.box_y + (y - dl.box_y) * k, 0, sx * dl.scale, sy * dl.scale, tint, &in);
  gfx_hud(&in, 0);
}

/* Text YN's pieces: placed about the lower box's place at the text's scale */
static float yn_x(float x) { return BOX_X + (x - BOX_X) * TEXT_K; }
static float yn_y(float y) { return YN_BOX_Y + (y - YN_BOX_Y) * TEXT_K; }
static void yn_sprite(int sprite, float x, float y, float sx, uint8_t tint) {
  if (sprite < 0) return;
  Inst in;
  sprite_inst(sprite, yn_x(x), yn_y(y), 0, sx, fabsf(sx), tint, &in);
  gfx_hud(&in, 0);
}
/* a word centred on x, its top at the rect's (w x h about (x, y)) */
static void yn_word(int text, int style, float x, float y, float h, uint32_t rgb) {
  const uint8_t *s = text_get(text), *st = font_style(style);
  int n = (int)strlen((const char *)s);
  float px = VIEW_W / 2 + yn_x(x) * HUD_PX - text_width(style, s, n) / 2;
  gfx_text(style, px, VIEW_H / 2 - yn_y(y + h / 2) * HUD_PX + font_asc(st), s, n, 0xFF000000u | rgb, 0);
}

static void yn_draw(uint8_t white) {
  if (dl.geo_on) {
    /* the toll (Geo Text: the coin, the count in TrajanPro, its left at its place, middle) */
    yn_sprite(dl.coin.sprite, -1.22f, 2.2f, 1, white);
    char buf[8];
    int n = 0, v = dl.toll;
    do buf[n++] = (char)('0' + v % 10), v /= 10;
    while (v && n < 7);
    static const float adv[10] = DIGIT_ADV;
    float x = yn_x(-0.573f), y = yn_y(2.01f) + 0.23f;
    while (n--) {
      int d = buf[n] - '0';
      Inst in;
      sprite_inst(SPRITE_DIGIT0 + d, x, y, 0, 1, 1, white, &in);
      gfx_hud(&in, 0);
      x += adv[d];
    }
  }
  if (dl.pc != PC_YN_LIST && dl.pc != PC_YN_CHOSEN) return;
  /* the UI List: Yes (without the geo for it: Not Enough, grey, and its line), No; the pointers at the current one */
  static const UiPiece pl = SHOP_POINTER_L, pr = SHOP_POINTER_R;
  bool short_of = dl.toll > g_pd.geo;
  yn_word(TXT_YN_YES, STYLE_YN, -4.153f, 0.61f, 1.5622f, short_of ? 0xA5A5A5 : 0xFFFFFF);
  if (short_of) yn_word(TXT_YN_NOT_ENOUGH, STYLE_MSG, -4.033f, -0.465f, 1.5622f, 0xFFFFFF);
  yn_word(TXT_YN_NO, STYLE_YN, 3.687f, 0.61f, 1.5622f, 0xFFFFFF);
  float cx = dl.yn_cur == 1 ? -4.153f : 3.687f;
  yn_sprite(pl.sprite, cx - 2.014f, 0.81f, pl.kx * TEXT_K, white);
  yn_sprite(pr.sprite, cx + 2.104f, 0.81f, pr.kx * TEXT_K, white);
}

void dialogue_draw(void) {
  if (dl.alpha > 0 || dl.fleurs_on) {
    /* the backboard (0.6 of its color's alpha), the fleurs */
    uint8_t a = (uint8_t)(dl.alpha * 0.6f * 0.616f * 255);
    if (a) box_sprite(SPRITE_DIALOGUE_BACKBOARD, 0, dl.box_y - 0.09f, 1, 1.2527f / 2.325f, gfx_dyn_tint(18, 255, 255, 255, a));
    uint8_t white = gfx_dyn_tint(19, 255, 255, 255, 255);
    if (dl.fleurs_on) {
      box_sprite(dl.fleur_top.sprite, 0, dl.box_y + 2.85f, TEXT_K, TEXT_K, white);
      box_sprite(dl.fleur_bot.sprite, 0, dl.box_y - 2.73f, TEXT_K, TEXT_K, white);
    }
  }
  uint8_t white = gfx_dyn_tint(20, 255, 255, 255, 255);
  if (dl.dream_on && dl.dream.sprite >= 0) {
    /* (Box Dream: its place and scale in the box's, with the text's) */
    Inst in;
    sprite_inst(dl.dream.sprite, BOX_X + (0.1f - BOX_X) * TEXT_K, BOX_Y + (4.46f - BOX_Y) * TEXT_K, 0, 1.0745f * TEXT_K,
                1.0745f * TEXT_K, white, &in);
    gfx_hud(&in, 0);
  }
  if (dl.arrow_on && dl.arrow.sprite >= 0) marker(dl.arrow.sprite, 0.0069f, dl.place[2], white);
  if (dl.stop_on && dl.stop.sprite >= 0) marker(dl.stop.sprite, -0.0231f, dl.place[1], white);
  if (dl.yn) yn_draw(white);
  if (!dl.text) return;
  /* the page's lines, top left in the rect, as far as the typewriter shows */
  const uint8_t *st = font_style(STYLE_DIALOGUE);
  float w = TEXT_W * HUD_PX * TEXT_K, h = dl.text_h * HUD_PX * TEXT_K;
  float left = VIEW_W / 2 + TEXT_X * HUD_PX - w / 2;
  float y = VIEW_H / 2 - text_mid() * HUD_PX - h / 2 + font_asc(st);
  int i = dl.page_start[dl.page], last = page_last(), per = lines_per_page(st);
  for (int l = 0; l < per && i < last; l++) {
    int next, e = line_end(st, dl.text, i, w, &next);
    int n = (e < dl.visible ? e : dl.visible) - i;
    /* (centred: the whole line's place, as it is typed) */
    float x = dl.centre || dl.yn ? left + (w - text_width(STYLE_DIALOGUE, dl.text + i, e - i)) / 2 : left;
    if (n > 0) gfx_text(STYLE_DIALOGUE, x, y, dl.text + i, n, 0xFFFFFFFFu, 0);
    if (dl.visible <= e) break;
    i = next, y += font_line(st);
  }
}

void dialogue_reset(void) {
  memset(&dl, 0, sizeof dl);
  dl.scale = 0.75f;
  dl.box_y = BOX_Y, dl.text_y = TEXT_Y, dl.text_h = TEXT_H;
  dl.coin.sprite = -1;
  dl.arrow.sprite = dl.stop.sprite = dl.dream.sprite = -1;
  dl.place[0] = TEXT_Y, dl.place[1] = dl.place[2] = MARKER_Y;
}

/* ---------------------------------------------------------------- prompt markers */
#define MAX_PROMPTS 4
#define PROMPT_FADE_IN 0.4f
#define PROMPT_FADE_OUT 0.233f
typedef struct {
  bool used, visible;
  int16_t label;   /* its text */
  float x, y, t;
  Anim anim;
} Prompt;
static Prompt prompts[MAX_PROMPTS];

int prompt_show(int handle, int label, float x, float y) {
  /* ShowPromptMarker: the stored one again, or a new one */
  Prompt *p = handle >= 0 && handle < MAX_PROMPTS && prompts[handle].used ? &prompts[handle] : NULL;
  if (!p) {
    for (int i = 0; i < MAX_PROMPTS && !p; i++)
      if (!prompts[i].used) p = &prompts[i], handle = i;
    if (!p) return -1;
    memset(p, 0, sizeof *p);
    p->used = true;
  }
  p->label = (int16_t)label, p->x = x, p->y = y;
  anim_play_from_frame(&p->anim, CLIP_PROMPT_UP, 0);
  p->visible = true, p->t = 0;
  return handle;
}

void prompt_hide(int handle) {
  if (handle < 0 || handle >= MAX_PROMPTS || !prompts[handle].used || !prompts[handle].visible) return;
  Prompt *p = &prompts[handle];
  anim_play_from_frame(&p->anim, CLIP_PROMPT_DOWN, 0);
  p->visible = false, p->t = 0;   /* (recycled after the fade) */
}

void prompts_reset(void) { memset(prompts, 0, sizeof prompts); }

#ifdef HOST
/* (tests: the prompts up, their texts) */
int prompts_up(int16_t *labels, int max) {
  int n = 0;
  for (int i = 0; i < MAX_PROMPTS && n < max; i++)
    if (prompts[i].used && prompts[i].visible) labels[n++] = prompts[i].label;
  return n;
}
#endif

void prompts_tick(void) {
  for (int i = 0; i < MAX_PROMPTS; i++) {
    Prompt *p = &prompts[i];
    if (!p->used) continue;
    p->t += DT;
    anim_update(&p->anim, DT);
    if (!p->visible && p->t >= PROMPT_FADE_OUT) p->used = false;
  }
}

void prompts_draw(void) {
  for (int i = 0; i < MAX_PROMPTS; i++) {
    Prompt *p = &prompts[i];
    if (!p->used) continue;
    float a = p->visible ? fminf(1, p->t / PROMPT_FADE_IN) : fmaxf(0, 1 - p->t / PROMPT_FADE_OUT);
    Inst in;
    if (p->anim.sprite >= 0) {
      sprite_inst(p->anim.sprite, p->x, p->y, 0, 1, 1, 0, &in);   /* (the room's tint 0: white) */
      gfx_actor(&in, PROMPT_SORT);
    }
    /* the shadow (black, faded with the label) */
    sprite_inst(SPRITE_PROMPT_SHADOW, p->x - 0.14f, p->y + 0.42f, 0.01f, 1, 0.58f / 0.8f, gfx_dyn_tint(7, 0, 0, 0, (uint8_t)(a * 255)), &in);
    gfx_actor(&in, PROMPT_SORT);
    /* the label, centered on (0, 0.249) */
    const uint8_t *s = text_get(p->label);
    int n = (int)strlen((const char *)s);
    const uint8_t *st = font_style(STYLE_PROMPT);
    float k = FOCAL / (0 - CAM_Z);
    float cx = VIEW_W / 2 + (p->x - g_cam_x) * k, cy = VIEW_H / 2 - (p->y + 0.249f - g_cam_y) * k;
    gfx_text(STYLE_PROMPT, cx - text_width(STYLE_PROMPT, s, n) / 2, cy + font_asc(st) / 2, s, n,
             (uint32_t)(a * 255) << 24 | 0xFFFFFF, 1);
  }
}
