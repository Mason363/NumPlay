#ifndef TURBO_RACE_H
#define TURBO_RACE_H
#include <stdint.h>
#include <stdbool.h>
#define TR_TRACKS 6
#define TR_RACERS 8
#define TR_LAPS 3
#define TR_SAVE_BYTES 80
enum { TR_READY, TR_RACING, TR_FINISHED, TR_WRECKED };
enum { TR_GRAND_PRIX, TR_SINGLE_RACE, TR_FREE_RIDE };
enum { TR_THROTTLE=1, TR_BRAKE=2, TR_LEFT=4, TR_RIGHT=8, TR_BOOST=16 };
typedef struct { const char *name; uint16_t segment; uint8_t landscape; int8_t curves[32]; } TrTrack;
typedef struct {
  uint32_t money, best[TR_TRACKS], wins, races;
  uint8_t upgrades[4], paint, mode, track, mood, manual, difficulty, effects;
} TrProfile;
typedef struct { int32_t distance, speed; int16_t x, lateral; uint32_t finished; int16_t lane; uint8_t style, skill; } TrCar;
typedef struct {
  TrCar cars[TR_RACERS]; uint32_t seed, ticks, time, lap_time, best_lap;
  uint16_t countdown, nitro, damage, invulnerable, offroad_ticks;
  uint8_t track, mood, manual, difficulty, phase, gear, rank, lap, boost;
  int16_t steering; uint16_t contacts[7], impact, boost_empty;
  int16_t camera_x; uint8_t mode; uint32_t overtakes, distance_base;
  int16_t heading;
} TrRace;
extern const TrTrack tr_tracks[TR_TRACKS];
void tr_defaults(TrProfile *p);
void tr_new(TrRace *r,const TrProfile *p,unsigned track,uint32_t seed);
void tr_step(TrRace *r,const TrProfile *p,unsigned input,int shift);
void tr_interpolate(TrRace *out,const TrRace *previous,const TrRace *current,unsigned alpha);
int tr_curve(unsigned track,int32_t meters);
int tr_length(unsigned track);
int tr_speed_kmh(const TrRace *r);
unsigned tr_position(const TrRace *r,unsigned car);
int tr_gap(const TrRace *r,int32_t a,int32_t b);
int tr_top(const TrProfile *p);
bool tr_car_active(const TrRace *r,unsigned car);
uint32_t tr_distance(const TrRace *r);
unsigned tr_prize(unsigned rank);
unsigned tr_reward(TrProfile *p,const TrRace *r);
unsigned tr_price(const TrProfile *p,unsigned item);
bool tr_buy(TrProfile *p,unsigned item);
void tr_encode(const TrProfile *p,uint8_t out[TR_SAVE_BYTES]);
bool tr_decode(TrProfile *p,const uint8_t *in,unsigned n);
#endif
