#ifndef FOOTBALL_FC_H
#define FOOTBALL_FC_H
#include <stdint.h>
#include <stdbool.h>
#define FC_Q 256
#define FC_LENGTH 110
#define FC_HALF_WIDTH 37
#define FC_W (2*FC_HALF_WIDTH*FC_Q)
#define FC_H (FC_LENGTH*FC_Q)
#define FC_GOAL_HALF 1040
#define FC_GOAL_HEIGHT 680
#define FC_PEN_RUNUP 12
#define FC_PEN_SETTLE 42
#define FC_HELP_PAGES 8
enum { FC_PEN_WAIT,FC_PEN_GOAL,FC_PEN_SAVE,FC_PEN_MISS,FC_PEN_POST };
#define FC_TEAMS 32
#define FC_SAVE_BYTES 256
#define FC_MIN(a,b) ((a)<(b)?(a):(b))
#define FC_MAX(a,b) ((a)>(b)?(a):(b))
#define FC_CLAMP(a,l,h) FC_MIN(FC_MAX((a),(l)),(h))
enum { FC_LEFT=1,FC_RIGHT=2,FC_UP=4,FC_DOWN=8,FC_SPRINT=16,FC_SHOOT=32,FC_PASS=64,FC_SWITCH=128,
 FC_THROUGH=256,FC_LOB=512,FC_CONTROL=1024,FC_PRESS=2048,FC_TACTIC=4096 };
enum { FC_DRIBBLE,FC_GROUND,FC_RUN_PASS,FC_HIGH_PASS,FC_DRIVE,FC_FINESSE };
enum { FC_KICKOFF,FC_LIVE,FC_GOAL,FC_HALF,FC_FULL,FC_THROW,FC_CORNER,FC_FREE,FC_GOALKICK,FC_PENALTY };
enum { FC_FRIENDLY,FC_CAREER,FC_CUP,FC_SHOOTOUT,FC_TRAINING,FC_FREEPLAY };
typedef struct { const char *name,*code; uint32_t shirt,shorts,away; uint8_t attack,midfield,defense; } FCTeam;
extern const FCTeam fc_teams[FC_TEAMS];
typedef struct { int16_t x,y,vx,vy; uint16_t stamina; uint8_t cooldown,pose; } FCPlayer;
typedef struct { int32_t x,y,z,vx,vy,vz; int8_t owner,last,receiver; uint8_t grace; } FCBall;
typedef struct { int16_t face_x,face_y; uint8_t touch,action,think,skill; } FCMotion;
typedef struct {
 FCPlayer p[22]; FCBall ball; uint32_t seed,ticks,clock,duration; uint16_t timer,charge;
 uint8_t teams[2],score[2],shots[2],possession[2],phase,selected,difficulty,formation,half,set_team;
 uint8_t fouls[2],corners[2],pass_cool,notice; int16_t aim; uint16_t power;
 int8_t offside; uint8_t taker,mode,quality[2],previous_input;
 uint16_t shoot_taken[2]; uint8_t shoot_score[2],shoot_turn,shoot_round,shoot_done,shoot_result;
 int16_t shoot_keeper,shoot_target; uint16_t shoot_timer; uint8_t shoot_state;
 FCMotion motion[22];uint16_t input_latch,pass_charge;
 int8_t charge_owner;uint8_t pass_kind,ball_kind,control_lock;
 int16_t receive_x,receive_y,spin;
 uint8_t keeper_wait[2],keeper_dive[2];
 uint16_t passes[2],completed[2];
 uint16_t action_aim;
 uint8_t tactics[2],tactic_notice,drill,drill_target,drill_hits;
 int16_t shoot_height,shoot_start,shoot_goal,shoot_guard_z;
 uint8_t shoot_flight,shoot_resolved;
 uint16_t shoot_marks[2];
 uint8_t yellow[22],red[22],keeper_id[2],card_player,card_kind,card_timer,forfeit;
 uint8_t ai_delay[22],ai_stumble[22],feint_cool;
 int16_t ai_x[22],ai_y[22];int8_t tracked_owner,feint_x,feint_y;
 uint16_t kick_clock;uint8_t kick_quality,single_penalty;
 int16_t kick_height;int8_t curve;
 uint8_t wall_count,wall[4],wall_clock;
 int16_t free_x,free_y;uint16_t free_age,free_attempts,free_goals;
 uint8_t free_view,free_result,free_team;
} FCMatch;
typedef struct {
 uint32_t money,wins,played,seed; uint16_t season;
 uint8_t team,opponent,difficulty,length,formation,kit;
 uint8_t career_active,career_team,career_round,coach,training;
 uint8_t roster[11],contracts[11],league[8],pts[8],gf[8],ga[8];
 uint8_t cup_active,cup_team,cup_round,cup_stage,cup_alive;
 uint8_t group_goals[48][2],bracket[16],knock_goals[15][2];
 uint16_t trophies; uint8_t last_rank,venue;
} FCProfile;
uint32_t fc_random(uint32_t *seed);
int fc_distance(int x,int y);
void fc_defaults(FCProfile *p);
void fc_new(FCMatch *m,const FCProfile *p,unsigned mode,unsigned opponent,uint32_t seed);
void fc_step(FCMatch *m,unsigned input);
static inline bool fc_active(const FCMatch*m,int i){
 return i>=0&&i<22&&!m->red[i]&&(m->mode!=FC_TRAINING||(m->drill==0?(i==9||i==11):m->drill==1?(i==5||i==7||i==9):i==9));
}
static inline int fc_keeper(const FCMatch*m,int team){return m->keeper_id[team];}
static inline bool fc_free_view(const FCMatch*m){return m->phase==FC_FREE||m->free_view==1;}
void fc_free_reset(FCMatch*m);
void fc_free_update(FCMatch*m);
void fc_free_basis(const FCMatch*m,int*ox,int*oy,int*fx,int*fy);
void fc_foul(FCMatch*m,int offender,int victim,unsigned severity);
void fc_wall_setup(FCMatch*m);
int fc_wall_lift(const FCMatch*m,int player);
unsigned fc_timing(unsigned clock);
void fc_free_velocity(const FCMatch*m,int*vx,int*vy,int*vz);
void fc_free_step(FCMatch*m,unsigned in,unsigned hit);
void fc_start_match_penalty(FCMatch*m,int team);
const char*fc_tactic_name(unsigned tactic);
const char*fc_drill_name(unsigned drill);
void fc_training_new(FCMatch*m,const FCProfile*p,unsigned drill,uint32_t seed);
void fc_training_reset(FCMatch*m);
void fc_training_update(FCMatch*m);
void fc_training_target(const FCMatch*m,int*x,int*y);
int fc_attack(const FCMatch *m,int team);
void fc_pass(FCMatch *m,int player);
void fc_pass_type(FCMatch *m,int player,unsigned kind,unsigned power);
int fc_pass_target(const FCMatch *m,int player,unsigned kind);
void fc_receive_target(const FCMatch*m,int player,int*x,int*y);
void fc_shot(FCMatch *m,int player,unsigned power);
void fc_possess(FCMatch *m,int player);
void fc_tackle(FCMatch *m,int player);
void fc_ai(FCMatch *m,unsigned input);
void fc_move_to(FCMatch *m,int player,int x,int y,int speed);
void fc_interpolate(FCMatch *out,const FCMatch *previous,const FCMatch *current,unsigned alpha);
void fc_restart(FCMatch *m,int phase,int team,int x,int y);
void fc_set_target(const FCMatch *m,int *x,int *y);
int fc_nearest(const FCMatch *m,int team,int x,int y,bool keeper);
void fc_start_penalties(FCMatch *m);
void fc_penalty_step(FCMatch *m,unsigned input);
void fc_career_new(FCProfile *p,unsigned team);
void fc_league_pair(unsigned round,unsigned pair,unsigned *a,unsigned *b);
unsigned fc_career_opponent(const FCProfile *p);
void fc_finish(FCProfile *p,const FCMatch *m);
unsigned fc_rank(const FCProfile *p,unsigned slot);
unsigned fc_train_price(const FCProfile *p,unsigned player);
bool fc_train(FCProfile *p,unsigned player);
bool fc_contract(FCProfile *p,unsigned player,unsigned games);
bool fc_coach(FCProfile *p);
bool fc_recruit(FCProfile *p,unsigned offer);
void fc_career_next(FCProfile *p);
void fc_cup_new(FCProfile *p,unsigned team);
void fc_group_table(const FCProfile *p,unsigned group,uint8_t pts[4],uint8_t gf[4],uint8_t ga[4]);
unsigned fc_cup_opponent(const FCProfile *p);
void fc_encode(const FCProfile *p,uint8_t out[FC_SAVE_BYTES]);
bool fc_decode(FCProfile *p,const uint8_t *data,unsigned size);
#endif
