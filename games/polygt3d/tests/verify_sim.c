#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLATFORM_DEVICE 1
#define main app_entry_main
#include "../src/main.c"
#undef main

static eadk_color_t full_screen[SCREEN_W * SCREEN_H];
static uint32_t mock_millis = 0;
static eadk_keyboard_state_t mock_kb = 0;

void eadk_display_push_rect(eadk_rect_t rect, const eadk_color_t *pixels) {
  for (int y = 0; y < rect.height; y++) {
    int sy = rect.y + y;
    if (sy >= 0 && sy < SCREEN_H) {
      for (int x = 0; x < rect.width; x++) {
        int sx = rect.x + x;
        if (sx >= 0 && sx < SCREEN_W) {
          full_screen[sy * SCREEN_W + sx] = pixels[y * rect.width + x];
        }
      }
    }
  }
}

eadk_keyboard_state_t eadk_keyboard_scan(void) { return mock_kb; }
uint64_t eadk_timing_millis(void) { return mock_millis; }
void eadk_timing_msleep(uint32_t ms) { mock_millis += ms; }

static void save_ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  assert(f != NULL);
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t c = full_screen[i];
    uint8_t r = ((c >> 11) & 0x1F) << 3;
    uint8_t g = ((c >> 5) & 0x3F) << 2;
    uint8_t b = (c & 0x1F) << 3;
    fputc(r, f);
    fputc(g, f);
    fputc(b, f);
  }
  fclose(f);
}

int main(void) {
  // 1. Stress-test 50 random track seeds: verify ZERO knots, ZERO upside-down/bowtie quads!
  const float gw = ROAD_HALF_W + 6.5f;
  for (uint32_t seed = 1; seed <= 50; seed++) {
    build_track_seeded(seed % 3, seed * 7919u);
    for (int i = 0; i < TRACK_N; i++) {
      int next = (i + 1) % TRACK_N;
      const TrackNode *n0 = &track_nodes[i];
      const TrackNode *n1 = &track_nodes[next];

      // Tangent dot product must be > 0.90 (smooth turns, never folds back)
      float dot_t = n0->tx * n1->tx + n0->tz * n1->tz;
      assert(dot_t > 0.90f && "Track segment angle too sharp / knotted!");

      // Verify left and right outer grass edges advance forward along tangent (no bowtie inversion)
      float gl_dx = (n1->x - n1->nx * gw) - (n0->x - n0->nx * gw);
      float gl_dz = (n1->z - n1->nz * gw) - (n0->z - n0->nz * gw);
      float gr_dx = (n1->x + n1->nx * gw) - (n0->x + n0->nx * gw);
      float gr_dz = (n1->z + n1->nz * gw) - (n0->z + n0->nz * gw);
      assert((gl_dx * n0->tx + gl_dz * n0->tz) > 0.5f && "Left shoulder quad inverted!");
      assert((gr_dx * n0->tx + gr_dz * n0->tz) > 0.5f && "Right shoulder quad inverted!");
    }
  }

  // 2. Verify Smooth Steering Dynamics & ZERO static phantom drift:
  build_track_seeded(0, 2026);
  race_npc_count = 0;
  reset_race_state(0);
  float start_sin = player_sin_yaw;
  float start_cos = player_cos_yaw;
  for (int step = 0; step < 25; step++) {
    mock_millis += 33;
    step_physics(0.033f, false, false, true, false, false, false, false, false, mock_millis);
    if (fabsf_fast(player_offset) < ROAD_HALF_W) {
      assert(fabsf_fast(player_sin_yaw - start_sin) < 1e-5f && "Phantom static steering detected!");
      assert(fabsf_fast(player_cos_yaw - start_cos) < 1e-5f && "Phantom static steering detected!");
    }
  }
  // Verify progressive steering ramp and smooth return (no instant on/off snap)
  step_physics(0.033f, false, true, true, false, false, false, false, false, mock_millis);
  assert(player_steer > 0.05f && player_steer < 0.25f && "Steering must build progressively on press!");
  float prev_steer = player_steer;
  step_physics(0.033f, false, false, true, false, false, false, false, false, mock_millis);
  assert(player_steer < prev_steer && player_steer >= 0.0f && "Steering must smoothly decay toward center!");

  // 3. Verify Winner Finish Position Bug Fix: when player crosses finish line ahead of all NPCs, position == 1!
  race_npc_count = 5;
  race_total_laps = 1;
  reset_race_state(1000);
  // Place player at segment TRACK_N - 2 about to cross the finish line ahead of NPCs (lap 0)
  player_seg = TRACK_N - 2;
  player_wx = track_nodes[player_seg].x;
  player_wz = track_nodes[player_seg].z;
  player_sin_yaw = track_nodes[player_seg].tx;
  player_cos_yaw = track_nodes[player_seg].tz;
  player_speed = 75.0f;
  for (int step = 0; step < 15 && !race_finished; step++) {
    mock_millis += 33;
    step_physics(0.033f, false, false, true, false, false, false, false, false, mock_millis);
  }
  assert(race_finished && "Player should have finished 1-lap race!");
  assert(final_finish_position == 1 && player_position == 1 && "Winner finish position must be 1st!");

  // 4. Verify Infinity Driving Mode: zero bots, full nitro, streaming endless procedural road & biome transitions!
  toggle_infinity_mode();
  assert(infinity_mode && race_npc_count == 0 && "Infinity mode must disable bots!");
  reset_race_state(2000);
  assert(player_boost_fuel == 100.0f && "Infinity mode starts with 100% nitro!");
  uint32_t initial_gen = inf_gen_count;
  uint8_t start_biome = track_nodes[player_seg].biome_id;
  bool saw_new_biome = false;
  bool saw_elev_change = false;
  float start_y = track_nodes[player_seg].y;
  // Simulate driving forward along the streaming road across 120 segments (>2 biomes)
  for (int step = 0; step < 120; step++) {
    int next_seg = (player_seg + 1) % TRACK_N;
    // Check that consecutive streamed road segments never fold or invert
    float dx = track_nodes[next_seg].x - track_nodes[player_seg].x;
    float dz = track_nodes[next_seg].z - track_nodes[player_seg].z;
    float dot = dx * track_nodes[player_seg].tx + dz * track_nodes[player_seg].tz;
    assert(dot > 5.0f && "Streamed infinity road segment must advance forward without folding!");
    player_wx = track_nodes[next_seg].x;
    player_wz = track_nodes[next_seg].z;
    player_sin_yaw = track_nodes[next_seg].tx;
    player_cos_yaw = track_nodes[next_seg].tz;
    player_speed = 75.0f;
    mock_millis += 33;
    step_physics(0.033f, false, false, true, false, false, false, false, false, mock_millis);
    if (track_nodes[player_seg].biome_id != start_biome) saw_new_biome = true;
    if (fabsf_fast(track_nodes[player_seg].y - start_y) > 2.5f) saw_elev_change = true;
  }
  assert(inf_gen_count > initial_gen + 100 && "Infinity mode must continuously stream new segments ahead!");
  assert(saw_new_biome && "Infinity mode must transition across terrain biomes!");
  assert(saw_elev_change && "Infinity mode must generate changing hills and valleys!");
  assert(!race_finished && current_lap >= 2 && "Infinity mode must never trigger race_finished and must advance zones!");
  render_full_frame(false, false, mock_millis);
  toggle_infinity_mode();

  // 5. Render Race Setup & Active Race with Boost Orbs & NPCs
  race_total_laps = 3;
  race_npc_count = 5;
  render_menu_frame(2.85f, SCREEN_RACE_SETUP);
  save_ppm("preview_setup.ppm");

  // Verify Black Stallion (car_id 3) and Brown Mustang (car_id 4) render cleanly
  selected_car_id = 3;
  render_menu_frame(2.40f, SCREEN_GARAGE);
  assert(num_screen_tris > 40);

  selected_car_id = 4;
  render_menu_frame(2.40f, SCREEN_GARAGE);
  assert(num_screen_tris > 40);
  save_ppm("preview_horse_garage.ppm");

  render_menu_frame(2.40f, SCREEN_CONTROLS);
  save_ppm("preview_controls.ppm");

  reset_race_state(mock_millis);
  for (int step = 0; step < 12; step++) {
    mock_millis += 33;
    frame_counter++;
    step_physics(0.033f, false, false, true, false, false, step > 5, false, false, mock_millis);
  }
  render_full_frame(false, false, mock_millis);
  assert(num_screen_tris > 250 && num_screen_tris < MAX_SCREEN_TRIS);
  save_ppm("preview_race_npcs.ppm");

  printf("[PASS] Black Stallion & Brown Mustang, Infinity Mode, Steering & Orbs verified! Tris: %d\n",
         num_screen_tris);
  return 0;
}
