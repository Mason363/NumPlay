/* Host runner: plays the game with scripted keys and saves screenshots.
 *   play DATA.bin [--frames N] [--keys "F0-F1:keys,..."] [--shot F:path ...] [--room NAME] [--chapter N]
 *        [--tp F:X,Y or F0-F1:X,Y ...] (the player moved there at frame F, or kept there from F0 to F1)
 *        [--draw N: draw every N frames (else only for shots and the last frame)]
 *        [--load F:ROOM:INTRO:FX:FY: at frame F, LoadLevel(INTRO) into ROOM at the spawn nearest (FX, FY) of it]
 *        [--record A-B:DIR: every frame from A to B as DIR/NNNNN.ppm (for the README's GIFs)]
 *        [--nowipe: no wipe into the room] [--cam X,Y: the view's top-left there (in the room) when drawn]
 * keys: l r u d j(ump) x(dash) g(rab) p(ause) o(k) b(ack) n (journal) h(ome) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/celeste.h"
#include "../src/level.h"
#include "../src/text.h"
#include "../src/player.h"
#include "../src/entities.h"
#include "../src/wipe.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void host_save_dir(const char *d);
extern bool g_running;
void game_save(void);

static uint32_t parse_keys(const char *s) {
  uint32_t k = 0;
  for (; *s && *s != ','; s++) switch (*s) {
      case 'l': k |= K_LEFT; break;
      case 'r': k |= K_RIGHT; break;
      case 'u': k |= K_UP; break;
      case 'd': k |= K_DOWN; break;
      case 'j': k |= K_JUMP; break;
      case 'x': k |= K_DASH | K_TALK; break;
      case 'g': k |= K_GRAB; break;
      case 'p': k |= K_PAUSE; break;
      case 'b': k |= K_BACK; break;
      case 'n': k |= K_JOURNAL; break;
      case 'o': k |= K_OK; break;
      case 'h': k |= K_HOME; break;
    }
  return k;
}

long g_dbg_layers, g_dbg_pixels, g_dbg_prep, g_dbg_prep_n;
int main(int argc, char **argv) {
  if (argc < 2) return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc(n);
  fread(data, 1, n, f);
  fclose(f);
  cel_bin = data;
  int tp_at[64], tp_to[64], ntp = 0;
  float tp_x[64], tp_y[64];
  int frames = 120, chapter = -1, area = -1, checkpoint = -1, at_set = 0, dash_code = 0, list = 0, draw = 0, load_at = -1, load_intro = 0,
      nowipe = 0;
  const char *load_room = NULL, *rec_dir = NULL;
  int rec_from = -1, rec_to = -1;
  float load_fx = 0, load_fy = 1;
  float at_x = 0, at_y = 0, cam_x = 0, cam_y = 0;
  int cam_set = 0;
  const char *keys = "", *room = NULL, *saves = "build/no-saves", *say = NULL;
  const char *shots[64];
  int shot_at[64], nshots = 0;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--frames")) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys")) keys = argv[++i];
    else if (!strcmp(argv[i], "--room")) room = argv[++i];
    else if (!strcmp(argv[i], "--chapter")) chapter = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--area")) area = atoi(argv[++i]);   /* (the chapter knows its area) */
    else if (!strcmp(argv[i], "--checkpoint")) checkpoint = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--saves")) saves = argv[++i];
    else if (!strcmp(argv[i], "--say")) say = argv[++i];
    else if (!strcmp(argv[i], "--dash-code")) dash_code = 1;
    else if (!strcmp(argv[i], "--nowipe")) nowipe = 1;
    else if (!strcmp(argv[i], "--cam")) sscanf(argv[++i], "%f,%f", &cam_x, &cam_y), cam_set = 1;
    else if (!strcmp(argv[i], "--draw")) draw = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--record")) {
      char *a = argv[++i];
      rec_from = atoi(a);
      rec_to = atoi(strchr(a, '-') + 1);
      rec_dir = strchr(a, ':') + 1;
    } else if (!strcmp(argv[i], "--load")) {
      char *a = argv[++i];
      load_at = atoi(a);
      a = strchr(a, ':') + 1;
      char *b = strchr(a, ':');
      *b = 0;
      load_room = a;
      sscanf(b + 1, "%d:%f:%f", &load_intro, &load_fx, &load_fy);
    }
    else if (!strcmp(argv[i], "--list")) list = 1;
    else if (!strcmp(argv[i], "--at")) sscanf(argv[++i], "%f,%f", &at_x, &at_y), at_set = 1;
    else if (!strcmp(argv[i], "--tp") && ntp < 64) {
      const char *a = argv[++i];
      if (sscanf(a, "%d-%d:%f,%f", &tp_at[ntp], &tp_to[ntp], &tp_x[ntp], &tp_y[ntp]) != 4)
        sscanf(a, "%d:%f,%f", &tp_at[ntp], &tp_x[ntp], &tp_y[ntp]), tp_to[ntp] = tp_at[ntp];
      ntp++;
    }
    else if (!strcmp(argv[i], "--shot")) {
      char *s = argv[++i], *c = strchr(s, ':');
      shot_at[nshots] = atoi(s);
      shots[nshots++] = c + 1;
    }
  }
  (void)area;
  host_save_dir(saves);   /* --saves DIR: load and keep the save there (none by default) */
  game_init();
  if (list) {   /* --list: every chapter's rooms, "chapter room" a line */
    for (int c = 0; c < chapter_count(); c++)
      for (int i = 0; chapter_room_name(c, i); i++) printf("%d %s\n", c, chapter_room_name(c, i));
    return 0;
  }
  /* --chapter: the chapter's index in data.bin (the order of MAPS); --room NAME, --checkpoint K */
  if (room || chapter >= 0 || checkpoint >= 0) {
    int c = chapter >= 0 ? chapter : g_session.chapter;
    session_start(c, checkpoint);
    if (room) {
      int r = chapter_find_room(c, room);
      if (r >= 0) g_session.level = (uint8_t)r;
    }
    level_start(room ? INTRO_NONE : session_intro(true));
    g_in_level = true, g_menu = 0;
  }
  if (nowipe) g_wipe.active = false;   /* --nowipe: the room at once (no wipe in) */
  if (at_set) {   /* --at X,Y: the player there (in the room) */
    g_player.ent->x = g_level.room->x + at_x, g_player.ent->y = g_level.room->y + at_y;
    g_level.cam = player_camera_target(&g_player);
  }
  if (say) textbox_say(say, NULL, NULL);   /* --say KEY: that dialog in a textbox */
  for (int fr = 0; fr < frames; fr++) {
    host_keys = 0;
    for (const char *p = keys; *p;) {
      int a = atoi(p), b = a;
      const char *dash = strchr(p, '-'), *colon = strchr(p, ':');
      if (!colon) break;
      if (dash && dash < colon) b = atoi(dash + 1);
      if (fr >= a && fr <= b) host_keys |= parse_keys(colon + 1);
      const char *comma = strchr(colon, ',');
      if (!comma) break;
      p = comma + 1;
    }
    if (getenv("COMPLETE_AT") && fr == atoi(getenv("COMPLETE_AT"))) level_complete_area(false, false);   /* the chapter's end */
    if (dash_code && fr >= 120 && fr < 126) {   /* --dash-code: the dash listeners hear U, L, DR, UR, L, UL */
      static const float d[6][2] = {{0, -1}, {-1, 0}, {0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-1, 0}, {-0.7071f, -0.7071f}};
      level_dash_listeners(v2(d[fr - 120][0], d[fr - 120][1]));
    }
    for (int k = 0; k < ntp; k++)   /* --tp F:X,Y or F0-F1:X,Y (up to 64): at frame F, the player there (in the room), still */
      if (fr >= tp_at[k] && fr <= tp_to[k] && g_player.ent && g_level.room) {
        g_player.ent->x = g_level.room->x + tp_x[k], g_player.ent->y = g_level.room->y + tp_y[k];
        g_player.speed = v2(0, 0);
      }
    if (fr == load_at) level_load_room_near(chapter_find_room(g_session.chapter, load_room), load_intro, load_fx, load_fy);
    game_frame();
    if (getenv("DUSTDBG") && fr % 30 == 0) {
      g_dbg_layers = g_dbg_pixels = g_dbg_prep = g_dbg_prep_n = 0;
      game_draw();
      printf("f%d preps %ld entries %ld blit_layers %ld pixels %ld cam %.0f,%.0f player %.0f,%.0f\n", fr, g_dbg_prep, g_dbg_prep_n, g_dbg_layers, g_dbg_pixels, g_level.cam.x,
             g_level.cam.y, g_player.ent->x, g_player.ent->y);
    }
    host_time += 17;
    if (!g_running) {   /* Home: saved, and out */
      game_save();
      break;
    }
    if (getenv("CAMDBG") && fr == 0 && g_level.room)
      printf("bounds %d %d %d %d\n", g_level.room->x, g_level.room->y, g_level.room->w, g_level.room->h);
    if (getenv("CAMDBG") && g_player.ent)
      printf("f%d room %s cam %.2f,%.2f player %.1f,%.1f tr %d at %.3f st %d\n", fr, level_room_name(), g_level.cam.x, g_level.cam.y,
             g_player.ent->x, g_player.ent->y, g_level.transitioning, g_level.tr_at, g_player.state);
    if (getenv("TRACE") && g_player.ent)
      printf("f%d pos %.0f,%.0f sp %.1f,%.1f st %d gr %d dash %d stam %.0f anim %d keys %x\n", fr, g_player.ent->x, g_player.ent->y,
             g_player.speed.x, g_player.speed.y, g_player.state, g_player.on_ground, g_player.dashes, g_player.stamina,
             g_player.spr.anim, host_keys);
    bool want = false;
    for (int i = 0; i < nshots; i++)
      if (shot_at[i] == fr) want = true;
    bool rec = rec_dir && fr >= rec_from && fr <= rec_to;
    if (want || rec || fr == frames - 1 || (draw && fr % draw == 0)) {
      if (cam_set) g_level.cam = v2(g_level.room->x + cam_x, g_level.room->y + cam_y);   /* --cam X,Y: the view there */
      game_draw();
      if (rec) {
        char path[512];
        snprintf(path, sizeof path, "%s/%05d.ppm", rec_dir, fr);
        host_shot(path);
      }
      for (int i = 0; i < nshots; i++)
        if (shot_at[i] == fr) host_shot(shots[i]);
    }
  }
  printf("room %s player %.0f,%.0f state %d dead %d\n", level_room_name(), g_player.ent ? g_player.ent->x : 0,
         g_player.ent ? g_player.ent->y : 0, g_player.state, g_player.dead);
  printf("session: chapter %d area %d mode %d deaths %u time %u berries %llx; save berries %llx deaths %u\n",
         g_session.chapter, g_session.area, g_session.mode, g_session.deaths, g_session.time,
         (unsigned long long)g_session.berries, (unsigned long long)save_mode()->berries, save_mode()->deaths);
  printf("keys %d %d %d %d\n", g_bind[0], g_bind[1], g_bind[2], g_bind[3]);
  return 0;
}
