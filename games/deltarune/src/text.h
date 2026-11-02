/* Deltarune: text, typed a letter at a time, as the original's writer does it.
 *
 * The texts are the original's own (its lang_en.json), with its marks:
 *   &      a new line (indented under "* " when the text starts so)
 *   /      the end of a message: Z goes on to the next one
 *   /%     the end of the last: Z closes the box
 *   %      the next message at once; %% the box closes at once
 *   ^N     a pause (N: 1-9, 5 to 150 frames)
 *   \Ex    the face's expression (0-9, A-E); \Fx the face (0 none, S Susie, R Ralsei, N Noelle, T Toriel,
 *          L Lancer, A Alphys, B Berdly, r Rudy, K the King, ...)
 *   \cx    colour (R, B, Y, G, W, X black, 0 back to the text's own)
 *   \Cn    a choice of n (2-4) in the box (text_set_choices gives them)
 *   \Mn    a cue for the cutscene (text_cue())
 *   \Tx \Sn \In \sn \fn   the voice, a sound, a picture, skipping, a small face: shown or skipped here
 * Lines wrap at 33 letters (26 beside a face), every letter 8 pixels wide. */
#ifndef DR_TEXT_H
#define DR_TEXT_H
#include <stdbool.h>
#include <stdint.h>

enum { FC_NONE, FC_SUSIE, FC_RALSEI, FC_NOELLE, FC_TORIEL, FC_LANCER, FC_SANS, FC_UNDYNE = 9, FC_ASGORE,
       FC_ALPHYS, FC_BERDLY, FC_C, FC_JOCK, FC_RUDY, FC_CATTY, FC_BRATTY, FC_RURUS, FC_BURGERPANTS, FC_KING };

#define MAX_MSG 16

/* the dialogue box: messages added, then opened */
void text_clear(void);
void text_add(const char *s);                 /* a message (global.msg[n]) */
void text_addf(const char *fmt, ...);         /* (one made with ~1 ~2 filled, as scr_84_get_subst_string) */
void text_face(int fc, int fe);               /* the face it starts with (global.fc, global.fe) */
void text_open(void);
void text_side(int side);                     /* -1 by where Kris is (the default), 0 top, 1 bottom */
void text_set_choices(const char *a, const char *b, const char *c, const char *d);
bool text_active(void);
void text_update(uint32_t pressed, uint32_t held);
void text_draw(void);
int text_choice(void);                        /* the last choice: 0-3 */
int text_cue(void);                           /* the last \M cue (global.flag[20]) */
void text_set_cue(int v);
int text_msgno(void);                         /* the message shown now */
void text_close(void);

/* a writer anywhere (the battle's text, a bubble): the same marks */
typedef struct {
  const char *msg[MAX_MSG];
  int nmsg, cur;
  int pos;                  /* letters of the message shown (source offset) */
  int wait;                 /* frames to the next letter */
  int x, y;                 /* where its first line starts (screen) */
  int charline;             /* letters a line */
  int vspace, hspace;
  int font;
  uint16_t color;
  bool halt;                /* the message is all shown: waits for Z */
  bool closing;             /* /%: Z closes */
  bool done;                /* closed */
  bool skippable;
  uint8_t rate;             /* frames a letter (scr_textsetup's rate) */
  bool aster;               /* the message starts with "* " */
  uint8_t fc, fe;
  int talk_t;               /* frames the mouth moves */
  char line[640];           /* the message as laid out: wraps made into & */
  int len;
} Writer;
void writer_start(Writer *w, int x, int y, int charline);
void writer_add(Writer *w, const char *s);
bool writer_update(Writer *w, uint32_t pressed, uint32_t held);   /* false once done */
void writer_draw(const Writer *w);
bool writer_talking(const Writer *w);
void writer_show_all(Writer *w);
int writer_lines(const Writer *w);          /* lines shown of its message (linecount + 1) */
#endif
