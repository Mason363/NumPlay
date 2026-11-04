/* Drawing at 8 bits a pixel into g_fb (320 x 240; the view's 320 x 180 at VIEW_Y). Pictures come from the appvars
 * (tools/pack_ce.py): sprites are rows of runs, tiles 8 x 8 bytes (0: see-through). */
#include "ce.h"

int g_camx, g_camy;
uint8_t g_draw_color;
#define FB(x, y) (g_fb + (VIEW_Y + (y)) * 320 + (x))

/* a sprite at view (x, y) (its frame's top left), clipped to the view */
static void blit_sprite(const uint8_t *s, int x, int y, uint8_t flags) {
  int fw = s[0], fh = s[1], x0 = s[2], y0 = s[3], w = s[4], h = s[5];
  const uint8_t *p = s + 6;
  bool fx = flags & DF_FLIPX, fy = flags & DF_FLIPY, solid = flags & DF_SOLID;
  uint8_t col = g_draw_color;
  int left = fx ? x + fw - x0 - w : x + x0;   /* the stored part's left on the view */
  if (left >= VIEW_W || left + w <= 0 || !h) return;
  int vy = fy ? y + fh - 1 - y0 : y + y0, dy = fy ? -1 : 1;
  uint8_t *row = g_fb + (VIEW_Y + vy) * 320;
  int step = fy ? -320 : 320;
  for (int r = 0; r < h; r++, vy += dy, row += step) {
    int n = *p++;
    if (vy < 0 || vy >= VIEW_H) {   /* skip the row */
      while (n--) p += 2 + p[1];
      continue;
    }
    int at = 0;
    while (n--) {
      at += p[0];
      int len = p[1];
      const uint8_t *px = p + 2;
      p += 2 + len;
      if (!fx) {
        int dx = x + x0 + at, a = 0, b = len;
        if (dx < 0) a = -dx;
        if (dx + b > VIEW_W) b = VIEW_W - dx;
        if (a < b) {
          if (solid) memset(row + dx + a, col, b - a);
          else if (g_remap)
            for (int i = a; i < b; i++) row[dx + i] = g_remap[px[i]];
          else memcpy(row + dx + a, px + a, b - a);
        }
      } else {
        /* stored pixel x0 + at + i goes to x + fw - 1 - (x0 + at + i) */
        int base = x + fw - 1 - x0 - at;
        uint8_t *d = row + base;
        for (int i = 0; i < len; i++, d--)
          if ((unsigned)(base - i) < VIEW_W) *d = solid ? col : g_remap ? g_remap[px[i]] : px[i];
      }
      at += len;
    }
  }
}

void draw_sprite(const uint8_t *s, int x, int y, uint8_t flags) {
  x -= g_camx, y -= g_camy;
  int l = x + s[2], t = y + s[3];
  if (!flags && !g_remap && l >= 0 && t >= 0 && l + s[4] <= VIEW_W && t + s[5] <= VIEW_H) {
    if (s[5]) blit_rle(g_fb + (VIEW_Y + t) * 320 + l, s + 6, s[5]);   /* (all of it on the view) */
    return;
  }
  blit_sprite(s, x, y, flags);
}

/* scaled (sx, sy: 1/256, nearest), its origin (ox, oy, frame pixels) at world (x, y) */
void draw_sprite_scaled(const uint8_t *s, int x, int y, int sx, int sy, int ox, int oy, uint8_t flags) {
  if (sx == 256 && sy == 256) {
    draw_sprite(s, x - ((flags & DF_FLIPX) ? s[0] - ox : ox), y - oy, flags);
    return;
  }
  /* decode into a buffer, then sample it */
  extern uint8_t *g_scratch;
  uint8_t *buf = g_scratch;   /* (32 x 32) */
  int fw = s[0], fh = s[1], x0 = s[2], y0 = s[3], h = s[5];
  if (fw > 32 || fh > 32) {
    draw_sprite(s, x - ((flags & DF_FLIPX) ? fw - ox : ox), y - oy, flags);
    return;
  }
  memset(buf, 0, (size_t)fw * fh);
  const uint8_t *p = s + 6;
  for (int r = 0; r < h; r++) {
    int n = *p++, at = 0;
    while (n--) {
      at += p[0];
      memcpy(buf + (y0 + r) * fw + x0 + at, p + 2, p[1]);
      at += p[1];
      p += 2 + p[1];
    }
  }
  bool fx = flags & DF_FLIPX;
  /* view pixel vx shows frame column u = ox + (rel * 256 + 128) / sx (rel from the origin, mirrored if flipped): a
   * column's u, a row's v, worked out once each */
  int bx = x - g_camx, by = y - g_camy;
  int dx0 = bx + fdiv(-ox * sx, 256), dx1 = bx + fdiv((fw - ox) * sx + 255, 256);
  int dy0 = by + fdiv(-oy * sy, 256), dy1 = by + fdiv((fh - oy) * sy + 255, 256);
  if (fx) {
    int a = bx - fdiv((fw - ox) * sx + 255, 256) + 1, b = bx - fdiv(-ox * sx, 256) + 1;
    dx0 = a, dx1 = b;
  }
  dx0 = imax(dx0, 0), dx1 = imin(dx1, VIEW_W);
  if (dx1 - dx0 > 64) dx1 = dx0 + 64;
  int8_t us[64];
  for (int vx = dx0; vx < dx1; vx++) {
    int rel = vx - bx;
    if (fx) rel = -rel;
    int u = ox + fdiv(rel * 256 + 128, sx);
    us[vx - dx0] = (int8_t)((unsigned)u < (unsigned)fw ? u : -1);
  }
  uint8_t *row = g_fb + (VIEW_Y + imax(dy0, 0)) * 320;
  for (int vy = imax(dy0, 0); vy < imin(dy1, VIEW_H); vy++, row += 320) {
    int v = oy + fdiv((vy - by) * 256 + 128, sy);
    if (v < 0 || v >= fh) continue;
    const uint8_t *src = buf + v * fw;
    for (int vx = dx0; vx < dx1; vx++) {
      int u = us[vx - dx0];
      if (u < 0) continue;
      uint8_t c = src[u];
      if (c) row[vx] = (flags & DF_SOLID) ? g_draw_color : g_remap ? g_remap[c] : c;
    }
  }
}

/* a tile at view (x, y), clipped */
static void blit_tile(const uint8_t *t, int x, int y, bool opaque) {
  int a = 0, b = 8, r0 = 0, r1 = 8;
  if (x < 0) a = -x;
  if (x + 8 > VIEW_W) b = VIEW_W - x;
  if (y < 0) r0 = -y;
  if (y + 8 > VIEW_H) r1 = VIEW_H - y;
  if (a >= b || r0 >= r1) return;
  for (int r = r0; r < r1; r++) {
    uint8_t *d = FB(x, y + r);
    const uint8_t *s = t + r * 8;
    if (opaque) memcpy(d + a, s + a, b - a);
    else
      for (int i = a; i < b; i++)
        if (s[i]) d[i] = s[i];
  }
}

void draw_tile(int tile, int x, int y) {
  if (tile) blit_tile(tile_data(tile), x - g_camx, y - g_camy, g_tile_flags[2 * tile] == 0xFF);
}

void draw_rect_screen(int x, int y, int w, int h, uint8_t c) {
  if (x < 0) w += x, x = 0;
  if (y < -VIEW_Y) h += y + VIEW_Y, y = -VIEW_Y;
  if (x + w > 320) w = 320 - x;
  if (y + h > 240 - VIEW_Y) h = 240 - VIEW_Y - y;
  if (w <= 0 || h <= 0) return;
  for (int r = 0; r < h; r++) memset(FB(x, y + r), c, w);
}
void draw_rect(int x, int y, int w, int h, uint8_t c) {
  x -= g_camx, y -= g_camy;
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > VIEW_W) w = VIEW_W - x;
  if (y + h > VIEW_H) h = VIEW_H - y;
  if (w <= 0 || h <= 0) return;
  for (int r = 0; r < h; r++) memset(FB(x, y + r), c, w);
}
void draw_pixel(int x, int y, uint8_t c) {
  x -= g_camx, y -= g_camy;
  if ((unsigned)x < VIEW_W && (unsigned)y < VIEW_H) *FB(x, y) = c;
}
void draw_line(int x0, int y0, int x1, int y1, uint8_t c) {
  int dx = iabs(x1 - x0), dy = -iabs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int guard = 0; guard < 2000; guard++) {
    draw_pixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}
void draw_circle_fill(int cx, int cy, int r, uint8_t c) {
  for (int y = -r; y <= r; y++) {
    int w = isqrt((int32_t)r * r - (int32_t)y * y);
    draw_rect(cx - w, cy + y, 2 * w + 1, 1, c);
  }
}
void draw_clear_screen(uint8_t c) { memset(g_fb, c, 320 * 240); }

/* ---------------------------------------------------------------- the tile layers */
/* the view's cells: each one's fg and bg tiles (the current room's grid over the other's), and where the background
 * shows; made again when the camera goes into other cells or the rooms change (g_cells_gen) */
extern uint8_t *g_cell_cache;
static int cache_c0 = 1 << 20, cache_r0;
static uint8_t cache_gen = 255;
static CellRow *cells_row(int r) { return (CellRow *)(void *)(g_cell_cache + r * sizeof(CellRow)); }
static void cells_build(int c0, int r0) {
  for (int r = 0; r < CROWS; r++) {
    CellRow *row = cells_row(r);
    memset(row, 0, sizeof *row);
    memset(row->vis, 1, COLS);
    for (int k = 0; k < 2; k++) {
      Room *rm = &g_level.rooms[k ? g_level.slot : g_level.slot ^ 1];   /* (the current one last: it wins) */
      if (rm->index < 0) continue;
      int ry = r0 + r - (rm->gy >> 3), gx = rm->gx >> 3;
      if ((unsigned)ry >= (unsigned)rm->th) continue;
      int a = imax(c0, gx), b = imin(c0 + COLS, gx + rm->tw);
      if (a >= b) continue;
      const uint8_t *gf = rm->grid + ry * rm->tw + (a - gx);
      build_cells(gf, gf + rm->tw * rm->th, rm->tab, row->fg + (a - c0), row->bg + (a - c0), row->vis + (a - c0),
                  (unsigned)(b - a));
    }
  }
  cache_c0 = c0, cache_r0 = r0, cache_gen = g_cells_gen;
}

/* a row of cells of a layer: the 39 inside the view at once, the two at its sides clipped */
static void layer_row(const Cell *cells, uint8_t *line, int offx, unsigned rows) {
  blit_row(cells + 1, COLS - 2, line + 8 - offx, rows);
  blit_clip(cells, line - offx, (unsigned)offx, 8, rows);
  if (offx) blit_clip(cells + COLS - 1, line + 320 - offx, 0, (unsigned)offx, rows);
}

static int view_offy;
static unsigned rows_mask(int r, uint8_t **line) {   /* row r of cells: its rows in the view, and its top's place */
  int vy = r * 8 - view_offy;
  int r0 = vy < 0 ? -vy : 0, r1 = vy + 8 > VIEW_H ? VIEW_H - vy : 8;
  *line = g_fb + (VIEW_Y + vy) * 320;
  return r0 >= r1 ? 0 : (0xFFu << r0) & (0xFFu >> (8 - r1));
}

void draw_layers_back(void) {
  int c0 = g_camx >> 3, r0 = g_camy >> 3, offx = g_camx & 7;
  view_offy = g_camy & 7;
  if (c0 != cache_c0 || r0 != cache_r0 || g_cells_gen != cache_gen) cells_build(c0, r0);
  /* the background's picture: view pixel (x, y) is its ((x + ox) % 320, y + oy), rows outside it the edge's */
  int ox = fdiv((g_camx - g_bg_refx) * g_bg_scrollx, 256), oy = fdiv((g_camy - g_bg_refy) * g_bg_scrolly, 256);
  ox %= 320;
  if (ox < 0) ox += 320;
  for (int r = 0; r < CROWS; r++) {
    uint8_t *line;
    unsigned rows = rows_mask(r, &line);
    if (!rows) continue;
    CellRow *row = cells_row(r);
    int vy = r * 8 - view_offy, y0 = imax(vy, 0), y1 = imin(vy + 8, VIEW_H);
    for (int c = 0; c < COLS;) {   /* the background, in runs of cells it shows in */
      if (!row->vis[c]) { c++; continue; }
      int e = c + 1;
      while (e < COLS && row->vis[e]) e++;
      int xa = imax(c * 8 - offx, 0), xb = imin(e * 8 - offx, VIEW_W);
      c = e;
      if (xa >= xb) continue;
      unsigned n = (unsigned)(xb - xa), sx = (unsigned)(xa + ox);
      if (sx >= 320) sx -= 320;
      unsigned first = n < 320 - sx ? n : 320 - sx;
      uint8_t *d = g_fb + (VIEW_Y + y0) * 320 + xa;
      if (!g_has_bg) {
        for (int y = y0; y < y1; y++, d += 320) memset(d, BLACK, n);
        continue;
      }
      /* its rows: those above the picture repeat its first, those below its last */
      for (int y = y0; y < y1;) {
        int sy = y + oy, k, step = 320;
        if (sy < 0) k = imin(y1 - y, -sy), sy = 0, step = 0;
        else if (sy >= VIEW_H) k = y1 - y, sy = VIEW_H - 1, step = 0;
        else k = imin(y1 - y, VIEW_H - sy);
        const uint8_t *src = g_bg8 + sy * 320;
        blit_rect(d, src + sx, first, (unsigned)k, (unsigned)step);
        if (first < n) blit_rect(d + first, src, n - first, (unsigned)k, (unsigned)step);
        y += k, d += 320 * k;
      }
    }
    layer_row(row->bg, line, offx, rows);
  }
}

void draw_layer_fg(void) {
  int offx = g_camx & 7;
  view_offy = g_camy & 7;
  for (int r = 0; r < CROWS; r++) {
    uint8_t *line;
    unsigned rows = rows_mask(r, &line);
    if (rows) layer_row(cells_row(r)->fg, line, offx, rows);
  }
}

/* the rooms' decals: u8 bg count, u8 fg count, then u16 sprite, s16 x, s16 y (centre), u8 flags, u8 frames */
void draw_decals(int fg) {
  for (int k = 0; k < 2; k++) {
    Room *rm = &g_level.rooms[k];
    if (rm->index < 0) continue;
    const uint8_t *d = rm->decals;
    int n = d[fg], skip = fg ? d[0] : 0;
    const uint8_t *p = d + 2 + 8 * skip;
    for (int i = 0; i < n; i++, p += 8) {
      int id = (int)R16(p), frames = p[7];
      if (frames > 1) id += (int)(g_level.frame / 5 % (unsigned)frames);   /* 12 a second */
      const uint8_t *s = csprite(id);
      int x = rm->x + S16(p + 2) - s[0] / 2, y = rm->y + S16(p + 4) - s[1] / 2;
      if (x - g_camx >= VIEW_W || y - g_camy >= VIEW_H || x + s[0] - g_camx <= 0 || y + s[1] - g_camy <= 0) continue;
      draw_sprite(s, x, y, p[6] & 3);
    }
  }
}

/* ---------------------------------------------------------------- text: a 5 x 7 font for now */
static const uint8_t FONT[96][5] = {
  {0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},{36,42,127,42,18},{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},
  {0,28,34,65,0},{0,65,34,28,0},{20,8,62,8,20},{8,8,62,8,8},{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},{32,16,8,4,2},
  {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
  {54,73,73,73,54},{6,73,73,41,30},{0,54,54,0,0},{0,86,54,0,0},{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},{2,1,81,9,6},
  {50,73,121,65,62},{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},
  {127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
  {127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},
  {99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},{0,127,65,65,0},{2,4,8,16,32},{0,65,65,127,0},{4,2,1,2,4},{64,64,64,64,64},
  {0,1,2,4,0},{32,84,84,84,120},{127,72,68,68,56},{56,68,68,68,32},{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},{12,82,82,82,62},
  {127,8,4,4,120},{0,68,125,64,0},{32,64,68,61,0},{127,16,40,68,0},{0,65,127,64,0},{124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},
  {124,20,20,20,8},{8,20,20,24,124},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},{60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},
  {68,40,16,40,68},{12,80,80,80,60},{68,100,84,76,68},{0,8,54,65,0},{0,0,127,0,0},{0,65,54,8,0},{16,8,8,16,8},{0,0,0,0,0}};
int text_width(const char *s) { return (int)strlen(s) * 6; }
void draw_text(const char *s, int x, int y, uint8_t c) {
  for (; *s; s++, x += 6) {
    int ch = (uint8_t)*s - 32;
    if (ch < 0 || ch >= 96) continue;
    for (int i = 0; i < 5; i++) {
      int bits = FONT[ch][i], sx = x + i;
      if ((unsigned)sx >= 320) continue;
      for (int j = 0; j < 7; j++)
        if (bits >> j & 1 && (unsigned)(y + j) < 240) g_fb[(y + j) * 320 + sx] = c;
    }
  }
}
