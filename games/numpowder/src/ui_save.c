/* NumPowder: the save slots (the core's simsave), the interface's choices (numpowder.cfg), and a copy of them in
 * a Python script (as games/finalearth2/src/savecopy.c): installing apps from the NumWorks website keeps only
 * Python scripts, so the saves are also kept in numpowder_saves.py, each as a comment line "#>name:base64", and
 * come back from it when the files themselves are gone. The copy gives way to the saves: when the storage is
 * too full for both, there is no copy. */
#include "ui.h"
#include <string.h>
#include "simsave.h"

static const char *const slot_files[NSLOTS + 1] = {"numpowder.sav", "numpowder1.sav", "numpowder2.sav",
                                                   "numpowder3.sav", "numpowder4.sav"};
#define CFG_NAME "numpowder.cfg"
#define COPY_NAME "numpowder_saves.py"
static const char *const kept[] = {"numpowder.cfg", "numpowder.sav", "numpowder1.sav", "numpowder2.sav",
                                   "numpowder3.sav", "numpowder4.sav"};
#define NKEPT (int)(sizeof kept / sizeof kept[0])

/* ---------------------------------------------------------------- the copy in a Python script */
static const char copy_head[] = "# NumPowder's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void save_copy(void) {
  /* (its size first, then written in place: the files read again, as writing it may have moved them) */
  uint32_t total = 1 + (sizeof copy_head - 1) + 1, len;
  int n = 0;
  for (int k = 0; k < NKEPT; k++)
    if (plat_load(kept[k], &len) && len) total += 2 + (uint32_t)strlen(kept[k]) + 1 + (len + 2) / 3 * 4 + 1, n++;
  plat_remove(COPY_NAME);   /* (the old copy's room counts for the new one) */
  if (!n) return;
  uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
  if (!o) return;
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  for (int k = 0; k < NKEPT; k++) {
    const uint8_t *d = plat_load(kept[k], &len);
    if (!d || !len) continue;
    uint32_t nl = (uint32_t)strlen(kept[k]);
    *p++ = '#', *p++ = '>';
    memcpy(p, kept[k], nl), p += nl;
    *p++ = ':';
    for (uint32_t i = 0; i < len; i += 3, p += 4) {
      /* (written out: GCC 15.2 for the calculator got a loop over the four wrong, see NumPlay's
       * launcher/src/storage.c) */
      uint32_t left = len - i, v = (uint32_t)d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
      p[0] = (uint8_t)b64[v >> 18 & 63];
      p[1] = (uint8_t)b64[v >> 12 & 63];
      p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
      p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
    }
    *p++ = '\n';
  }
  *p++ = 0;
  plat_reserved(COPY_NAME, (uint32_t)(p - o));
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}
/* where kept[k]'s line starts in the copy (just after its tag) and its length, or NULL */
static const uint8_t *copy_line(int k, uint32_t *len) {
  uint32_t n, nl = (uint32_t)strlen(kept[k]);
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return NULL;
  for (uint32_t i = 0; i + nl + 3 <= n; i++)
    if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, kept[k], nl) && c[i + 2 + nl] == ':') {
      const uint8_t *l = c + i + 3 + nl;
      uint32_t m = 0;
      while (l + m < c + n && l[m] != '\n' && l[m]) m++;
      *len = m;
      return l;
    }
  return NULL;
}
void saves_restore(void) {
  for (int k = 0; k < NKEPT; k++) {
    uint32_t have, len;
    if (plat_load(kept[k], &have)) continue;
    const uint8_t *l = copy_line(k, &len);
    if (!l || !len || len % 4) continue;
    uint32_t size = len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=');
    /* decoded straight into the file's room, then the copy read again (making the file may have moved it) */
    uint8_t *out = plat_reserve(kept[k], size);
    if (!out) continue;
    l = copy_line(k, &len);
    bool ok = l != NULL && len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=') == size;
    uint32_t o = 0;
    for (uint32_t i = 0; ok && i < len; i += 4) {
      int v0 = l[i] == '=' ? 0 : b64_value(l[i]), v1 = l[i + 1] == '=' ? 0 : b64_value(l[i + 1]);
      int v2 = l[i + 2] == '=' ? 0 : b64_value(l[i + 2]), v3 = l[i + 3] == '=' ? 0 : b64_value(l[i + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) ok = false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < size) out[o++] = (uint8_t)(w >> 16);
      if (o < size) out[o++] = (uint8_t)(w >> 8);
      if (o < size) out[o++] = (uint8_t)w;
    }
    if (ok && o == size) plat_reserved(kept[k], size);
    else plat_remove(kept[k]);
  }
}

/* ---------------------------------------------------------------- the interface's choices */
#define CFG_VERSION 1
enum { CF_HDISP_AUTO = 1, CF_PAUSED = 2 };   /* Dynamic Heat Display's range; the simulation paused */
typedef struct {
  uint8_t version, section, shape, slot;
  uint8_t heat, aheat, ngrav, water, air, gravity, edge, deco, gfield, debug, nfav, flags;   /* (flags: CF_*) */
  int16_t rx, ry, cx, cy;
  int32_t tool[2];
  uint32_t render_mode, display_mode, colour_mode;
  int32_t fav[NFAV];
} Config;

void config_save(void) {
  Config c;
  memset(&c, 0, sizeof c);
  c.version = CFG_VERSION, c.section = ui.section, c.shape = brush.shape, c.slot = ui.slot;
  c.heat = settings.heat, c.aheat = settings.ambient_heat, c.ngrav = settings.newtonian_gravity;
  c.water = settings.water_equalization, c.air = settings.air_mode, c.gravity = settings.gravity_mode;
  c.edge = settings.edge_mode, c.deco = ren.decorations, c.gfield = ren.grav_field, c.debug = ui.debug;
  c.rx = (int16_t)brush.rx, c.ry = (int16_t)brush.ry, c.cx = (int16_t)ui.cx, c.cy = (int16_t)ui.cy;
  c.tool[0] = ui.tool[0], c.tool[1] = ui.tool[1];
  c.render_mode = ren.render_mode, c.display_mode = ren.display_mode, c.colour_mode = ren.colour_mode;
  c.nfav = ui.nfav;
  c.flags = (uint8_t)((ren.hdisp_auto ? CF_HDISP_AUTO : 0) | (sys_pause ? CF_PAUSED : 0));
  for (int k = 0; k < ui.nfav; k++) c.fav[k] = ui.fav[k];
  plat_save(CFG_NAME, &c, sizeof c);
}

static bool tool_ok(int tool) {
  if (tool & TOOL_WALLS) return (tool & 0xFF) < UI_WALLCOUNT && !(tool & ~(TOOL_WALLS | 0xFF));
  if (tool & TOOL_SIMTOOLS) return (tool & 0xFF) < TOOL_COUNT && !(tool & ~(TOOL_SIMTOOLS | 0xFF));
  if (tool & TOOL_UI) return tool == TOOL_SAMPLE;
  return IsElementOrNone(TYP(tool)) && (TYP(tool) == PT_LIFE ? ID(tool) < NGOL : ID(tool) == 0);
}
void config_load(void) {
  uint32_t len;
  const uint8_t *d = plat_load(CFG_NAME, &len);
  if (!d || len != sizeof(Config)) return;
  Config c;
  memcpy(&c, d, sizeof c);
  if (c.version != CFG_VERSION) return;
  ui.section = c.section < SC_TOTAL ? c.section : SC_POWDERS;
  brush.shape = c.shape < NUM_BRUSHES ? c.shape : BRUSH_CIRCLE;
  ui.slot = c.slot <= NSLOTS ? c.slot : 0;
  settings.heat = c.heat, settings.ambient_heat = c.aheat, settings.newtonian_gravity = c.ngrav;
  settings.water_equalization = c.water;
  settings.air_mode = c.air < NUM_AIRMODES ? c.air : AIR_ON;
  settings.gravity_mode = c.gravity < NUM_GRAVMODES ? c.gravity : GRAV_VERTICAL;
  settings.edge_mode = c.edge < NUM_EDGEMODES ? c.edge : EDGE_SOLID;
  ren.decorations = c.deco, ren.grav_field = c.gfield, ui.debug = c.debug;
  if (c.rx >= 0 && c.rx <= 50 && c.ry >= 0 && c.ry <= 50) brush.rx = c.rx, brush.ry = c.ry;
  if (c.cx >= 0 && c.cx < XRES && c.cy >= 0 && c.cy < YRES) ui.cx = c.cx, ui.cy = c.cy;
  for (int s = 0; s < 2; s++)
    if (tool_ok(c.tool[s])) ui.tool[s] = c.tool[s];
  render_apply(c.render_mode, c.display_mode, c.colour_mode);
  ren.hdisp_auto = (c.flags & CF_HDISP_AUTO) != 0;
  sys_pause = (c.flags & CF_PAUSED) != 0;   /* (paused or not, kept across Home and starting again) */
  ui.nfav = 0;
  for (int k = 0; k < c.nfav && k < NFAV; k++)
    if (tool_ok(c.fav[k])) ui.fav[ui.nfav++] = c.fav[k];
  sim_settings_changed();
}

/* ---------------------------------------------------------------- the slots */
bool slot_used(int slot, int *nparts) {
  uint32_t len;
  const uint8_t *d = plat_load(slot_files[slot], &len);
  int n = d && len ? simsave_count(d, len) : -1;
  if (nparts) *nparts = n;
  return n >= 0;
}

int save_slot(int slot) {
  uint32_t n = simsave_size();
  if (!n) {
    config_save();
    save_copy();
    ui_message("Saving isn't ready yet");
    return SAVE_UNAVAILABLE;
  }
  plat_remove(COPY_NAME);   /* (the copy gives way to the save) */
  uint8_t *o = plat_reserve(slot_files[slot], n);
  bool ok = false;
  if (o) {
    uint32_t w = simsave_write(o, n);
    if (w) plat_reserved(slot_files[slot], w), ok = true;
    else plat_remove(slot_files[slot]);
  }
  config_save();
  save_copy();
  ui_message(ok ? (slot ? "Saved" : "Autosaved") : "No room to save: delete some files");
  return ok ? SAVE_OK : SAVE_NOROOM;
}

bool load_slot(int slot) {
  uint32_t len;
  const uint8_t *d = plat_load(slot_files[slot], &len);
  if (!d) {
    ui_message("Nothing saved there");
    return false;
  }
  if (!simsave_read(d, len)) {
    ui_message("That save couldn't be read");
    return false;
  }
  render_clear_accumulation();
  ui.slot = (uint8_t)slot;
  ui_message("Loaded");
  return true;
}

/* ---------------------------------------------------------------- the slots' window */
void saves_open(bool saving) {
  ui.saving = saving;
  ui.prev_mode = MODE_OPTIONS;
  ui.mode = MODE_SAVES;
}
static int slot_sel;
void saves_keys(void) {
  if (key_pressed(K_BACK)) {
    ui.mode = MODE_OPTIONS;
    return;
  }
  if (key_repeat(K_UP)) slot_sel = (slot_sel + NSLOTS) % (NSLOTS + 1);
  if (key_repeat(K_DOWN)) slot_sel = (slot_sel + 1) % (NSLOTS + 1);
  if (key_pressed(K_OK)) {
    ui.slot = (uint8_t)slot_sel;
    if (ui.saving) {
      if (slot_sel && slot_used(slot_sel, NULL)) {
        ui.prev_mode = MODE_SAVES;
        confirm_open(CONFIRM_OVERWRITE);
      } else {
        save_slot(slot_sel);
        ui.mode = MODE_SIM;
      }
    } else if (slot_used(slot_sel, NULL)) {
      ui.prev_mode = MODE_SAVES;
      confirm_open(CONFIRM_LOAD);
    } else {
      ui_message("Nothing saved there");
    }
  }
}
void saves_draw(Overlay *o) {
  int w = 200, h = 24 + (NSLOTS + 1) * 18 + 4, x = SIM_X + (SIM_W - w) / 2, y = (SIM_H - h) / 2;
  window_draw(o, x, y, w, h, ui.saving ? "Save the simulation" : "Load a simulation");
  for (int s = 0; s <= NSLOTS; s++) {
    int ry = y + 24 + s * 18;
    int nparts;
    bool used = slot_used(s, &nparts);
    char t[40], info[24] = "empty";
    if (used) {
      fmt_int(info, nparts);
      strcat(info, nparts == 1 ? " particle" : " particles");
    }
    if (s) {
      strcpy(t, "Slot ");
      char d[4];
      fmt_int(d, s);
      strcat(t, d);
    } else {
      strcpy(t, "Autosave \bg(Home)");
    }
    ov_rect(o, x + 6, ry, w - 12, 16, 0xC8C8C8, 255);
    ov_text(o, x + 10, ry + 4, t, 0xFFFFFF, 255);
    ov_text(o, x + w - 10 - font_text_width(info), ry + 4, info, used ? 0x20D8FF : 0x808080, 255);
    if (s == slot_sel) ov_frame(o, x + 6, ry, w - 12, 16, 2, 0xFFFF00);
  }
}
