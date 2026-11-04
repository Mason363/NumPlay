/* Plays Imposter on a computer, with scripted keys, and saves screenshots.
 *
 *   play [--frames N] [--keys "10:ok,20-30:down,..."] [--shots "40:setup.ppm,..."] [--out DIR] [--saves DIR]
 *        [--trace] [--no-typing] [--fuzz SEED] [--seed N] [--words] [--rounds N]
 *
 * A frame is 20 ms. Keys: left right up down ok exe back home erase, a to z (the letter keys), space. "a-b:key"
 * holds the key from frame a to b, "a:key" for one frame. --trace prints what each drawing shows (DRAW ...).
 * --fuzz presses random keys (Home now and then ends a session, the next one starts from the save).
 * --words checks the word lists; --rounds plays N rounds' draws straight away (ROUND ...).
 * At the end: STATE ... (the save). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "../src/plat.h"
#include "../src/ui.h"
#include "../../../launcher/src/font.h"

extern uint32_t host_keys, host_letters, host_time, host_seed;
extern bool host_typing;
void host_shot(const char *path);
void host_save_dir(const char *d);
int host_trace;

typedef struct {
  int a, b;
  uint32_t k, l;
} Hold;
static Hold holds[8192];
static int nholds;
typedef struct {
  int frame;
  char name[128];
} Shot;
static Shot shots[256];
static int nshots;
static char out_dir[512] = ".";

static void key_of(const char *s, uint32_t *k, uint32_t *l) {
  static const char *const names[] = {"left", "right", "up", "down", "ok", "exe", "back", "home", "erase"};
  static const uint32_t bits[] = {KB_LEFT, KB_RIGHT, KB_UP, KB_DOWN, KB_OK, KB_EXE, KB_BACK, KB_HOME, KB_ERASE};
  *k = *l = 0;
  for (int i = 0; i < 9; i++)
    if (!strcmp(s, names[i])) {
      *k = bits[i];
      return;
    }
  if (!strcmp(s, "space")) {
    *l = (uint32_t)1 << 26;
    return;
  }
  if (s[0] >= 'a' && s[0] <= 'z' && !s[1]) {
    *l = (uint32_t)1 << (s[0] - 'a');
    return;
  }
  fprintf(stderr, "unknown key %s\n", s);
  exit(2);
}

static void parse_keys(const char *spec) {
  char *buf = strdup(spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    int a, b;
    char name[16];
    uint32_t k, l;
    if (sscanf(tok, "%d-%d:%15s", &a, &b, name) == 3) key_of(name, &k, &l), holds[nholds++] = (Hold){a, b, k, l};
    else if (sscanf(tok, "%d:%15s", &a, name) == 2) key_of(name, &k, &l), holds[nholds++] = (Hold){a, a, k, l};
  }
  free(buf);
}

static void parse_shots(const char *spec) {
  char *buf = strdup(spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    Shot *s = &shots[nshots];
    if (sscanf(tok, "%d:%127s", &s->frame, s->name) == 2) nshots++;
  }
  free(buf);
}

static void print_state(void) {
  printf("STATE n %d names ", sv.nplayers);
  for (int i = 0; i < sv.nplayers; i++) printf("%s%s", i ? "," : "", sv.names[i]);
  printf(" imposters %d hint %d timer %d cats %u phase %d turn %d rounds %d word %d flags %d\n", sv.imposters, sv.hint,
         sv.timer, (unsigned)sv.cats, sv.phase, sv.turn, sv.rounds, sv.word, sv.flags);
}

/* the save always holds a game this one could have made */
static void invariants(int frame) {
  const char *bad = NULL;
  if (sv.nplayers > MAX_PLAYERS) bad = "players";
  for (int i = 0; i < sv.nplayers && !bad; i++) {
    size_t n = strlen(sv.names[i]);
    if (n < 1 || n > NAME_MAX || sv.names[i][0] == ' ' || sv.names[i][n - 1] == ' ') bad = "name";
    for (int j = 0; j < i && !bad; j++)
      if (!strcasecmp(sv.names[i], sv.names[j])) bad = "same names";
  }
  if (sv.phase) {
    int k = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) k += sv.imps >> i & 1;
    if (sv.nplayers < MIN_PLAYERS) bad = "round with too few players";
    if (k < 1 || k > max_imposters(sv.nplayers) || sv.imps >> sv.nplayers) bad = "imposters";
    if (sv.turn >= sv.nplayers || sv.starter >= sv.nplayers) bad = "turn";
    if ((sv.word >> 6) >= N_CATS || (int)(sv.word & 63) >= word_count(sv.word >> 6)) bad = "word";
    if (sv.cats && !(sv.cats >> (sv.word >> 6) & 1)) bad = "word outside the categories";
  }
  if (bad) {
    printf("ERROR frame %d: %s\n", frame, bad);
    print_state();
    exit(3);
  }
}

static int check_words(void) {
  int bad = 0, total = 0, lo = 999, hi = 0;
  char a[WORD_MAX + 1], b[WORD_MAX + 1];
  for (int c = 0; c < N_CATS; c++) {
    int n = word_count(c);
    total += n;
    lo = n < lo ? n : lo, hi = n > hi ? n : hi;
    printf("CAT %s %d\n", categories[c].name, n);
    for (int i = 0; i < n; i++) {
      int len = word_get(c, i, a, sizeof a);
      if (len < 2 || len > WORD_MAX - 1 || a[0] == ' ' || a[len - 1] == ' ') printf("BAD word %s/%d '%s'\n", categories[c].name, i, a), bad++;
      for (int k = 0; k < len; k++)
        if ((uint8_t)a[k] < 32 || (uint8_t)a[k] > 126) printf("BAD letter in %s\n", a), bad++;
      for (int d = 0; d <= c; d++)
        for (int j = 0, m = word_count(d); j < m && (d < c || j < i); j++) {
          word_get(d, j, b, sizeof b);
          if (!strcasecmp(a, b)) printf("BAD twice %s (%s, %s)\n", a, categories[d].name, categories[c].name), bad++;
        }
    }
  }
  printf("WORDS cats %d total %d min %d max %d bad %d\n", N_CATS, total, lo, hi, bad);
  return bad ? 1 : 0;
}

int main(int argc, char **argv) {
  int frames = 600, fuzz = -1, rounds = 0;
  uint32_t seed = 1;
  char saves[512] = "build/host-saves";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) parse_keys(argv[++i]);
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) parse_shots(argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) snprintf(out_dir, sizeof out_dir, "%s", argv[++i]);
    else if (!strcmp(argv[i], "--saves") && i + 1 < argc) snprintf(saves, sizeof saves, "%s", argv[++i]);
    else if (!strcmp(argv[i], "--trace")) host_trace = 1;
    else if (!strcmp(argv[i], "--no-typing")) host_typing = false;
    else if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) fuzz = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], 0, 10);
    else if (!strcmp(argv[i], "--words")) return check_words();
    else if (!strcmp(argv[i], "--rounds") && i + 1 < argc) rounds = atoi(argv[++i]);
    else {
      fprintf(stderr, "unknown option %s\n", argv[i]);
      return 2;
    }
  }
  host_save_dir(saves);
  host_seed = seed ? seed : 1;
  host_time = 1000;
  app_init(host_time);
  if (rounds) { /* the rules alone: the save's players and categories */
    for (int r = 0; r < rounds; r++) {
      round_new();
      char w[WORD_MAX + 1];
      int c = word_text(sv.word, w);
      printf("ROUND %d word %d %s cat %d %s imps %u starter %d\n", r, sv.word, w, c, categories[c].name,
             (unsigned)sv.imps, sv.starter);
      invariants(r);
    }
    print_state();
    return 0;
  }
  uint32_t fz = (uint32_t)(fuzz < 0 ? 1 : fuzz) * 2654435761u | 1, fkeys = 0, fletters = 0;
  int sessions = 1;
  for (int f = 0; f < frames; f++) {
    uint32_t k = 0, l = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) k |= holds[i].k, l |= holds[i].l;
    if (fuzz >= 0) {
      fz ^= fz << 13, fz ^= fz >> 17, fz ^= fz << 5;
      if (fz % 5 == 0) { /* a key held a few frames */
        static const uint32_t pool[] = {KB_LEFT, KB_RIGHT, KB_UP, KB_DOWN, KB_OK, KB_OK, KB_OK, KB_EXE, KB_BACK, KB_ERASE};
        uint32_t r = fz >> 8;
        fkeys = r % 41 == 0 ? KB_HOME : pool[r % 10];
        fletters = r % 3 == 0 ? (uint32_t)1 << (r / 7 % 27) : 0;
        if (r % 2) fkeys = 0;
      } else if (fz % 5 == 1) {
        fkeys = fletters = 0;
      }
      k |= fkeys, l |= fletters;
    }
    host_keys = k, host_letters = l;
    if (!app_step(k, l, host_time)) {
      printf("QUIT frame %d\n", f);
      if (fuzz < 0) {
        print_state();
        return 0;
      }
      sessions++;
      host_time += 20;
      app_init(host_time); /* the next session, from the save */
      fkeys = fletters = 0;
    }
    if (app_dirty()) app_draw();
    if (fuzz >= 0) invariants(f);
    for (int i = 0; i < nshots; i++)
      if (shots[i].frame == f) {
        char p[700];
        snprintf(p, sizeof p, "%s/%s", out_dir, shots[i].name);
        host_shot(p);
      }
    host_time += 20;
  }
  if (fuzz >= 0) printf("FUZZ sessions %d\n", sessions);
  game_save();
  print_state();
  return 0;
}
