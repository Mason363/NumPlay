// The lanes always leave a way forward (make test): 500 games of 1000 rows each, checked with the
// chicken's own moves. It goes sideways only through free grass cells and from pad to pad; it crosses a
// road or a rail straight (no sideways moves through the traffic); a log carries it with the river.
#include <eadk.h>
#include <stdio.h>
#include <string.h>
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *p) { (void)r, (void)p; }
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) { (void)r, (void)c; }
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *p) { (void)r, (void)p; }
void eadk_display_draw_string(const char *t, eadk_point_t p, bool l, eadk_color_t a, eadk_color_t b) {
  (void)t, (void)p, (void)l, (void)a, (void)b;
}
bool eadk_display_wait_for_vblank(void) { return true; }
eadk_keyboard_state_t eadk_keyboard_scan(void) { return 0; }
uint64_t eadk_timing_millis(void) { return 0; }
void eadk_timing_msleep(uint32_t ms) { (void)ms; }
void eadk_timing_usleep(uint32_t us) { (void)us; }
static uint32_t seed = 1;
uint32_t eadk_random(void) { return seed = seed * 1664525u + 1013904223u; }
bool eadk_usb_is_plugged(void) { return false; }
uint8_t eadk_battery_level(void) { return 100; }
const char *eadk_external_data = NULL;
size_t eadk_external_data_size = 0;

#define main crossy_main
#include "../src/crossy.c"
#undef main

int main(void) {
  memcpy(&G.rom, &rom_init, sizeof(rom_init));
  int fails = 0;
  for (int game = 0; game < 500 && fails < 10; game++) {
    seed = 7919u * (uint32_t)game + 1;
    new_game();
    uint16_t at = ALLPLAY;  // the columns the chicken can be in, in the lane behind
    for (int row = gen_next - 26; row < 1000 && fails < 10; row++) {
      while (gen_next <= row) gen_lane();
      const Lane *L = lane_at(row);
      if (row <= 0) continue;  // (the start)
      uint16_t free = 0;
      for (int i = 0; i < 9; i++) {
        int o = L->cells[COL0 + PLAY_MIN + i];
        if (L->type == L_GRASS ? !o : L->type != L_LILY || o == O_PAD) free |= 1 << i;
      }
      uint16_t now = at & free;  // straight on
      for (int k = 0; k < 9; k++) {
        if (L->type == L_GRASS || L->type == L_LILY) now |= ((now << 1) | (now >> 1)) & free;
        else if (L->type == L_RIVER) now |= (L->dir > 0 ? now << 1 : now >> 1) & ALLPLAY;
      }
      if (!now) {
        printf("game %d: no way through row %d (type %d), the chicken in %03x behind, free %03x\n", game, row, L->type,
               at, free);
        fails++;
        break;
      }
      at = now;
    }
  }
  printf(fails ? "FAILED\n" : "ok: 500 games of 1000 rows, always a way forward\n");
  return fails != 0;
}
