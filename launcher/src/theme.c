#include "theme.h"
static const np_theme_t themes[NP_THEME_COUNT] = {
  {"Starlight", 0x191027, 0x26183A, 0x33204D, 0xE89ACD, 0xFFF1FA, 0xCBB4F6, 0x7952A3},
  {"Literature Club", 0xFFF0F5, 0xF7DCEA, 0xE9ACCB, 0xBD77B0, 0x79456D, 0xBD77B0, 0xCBB4F6},
  {"Midnight Library", 0x101725, 0x1C2336, 0x30354C, 0x9C91DD, 0xEEE4FF, 0x9C91DD, 0xCBB4F6},
};
static int cur;
static bool loaded;
const np_theme_t *np_theme(void) {
  if (!loaded) {
    loaded = true;
    int id;
    if (np_theme_load(&id) && id >= 0 && id < NP_THEME_COUNT) cur = id;
  }
  return &themes[cur];
}
void np_theme_step(int dir) {
  np_theme();
  cur = (cur + dir + NP_THEME_COUNT) % NP_THEME_COUNT;
  np_theme_save(cur);
}
