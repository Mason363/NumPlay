/* The game: the Knight, what the player has, the camera, the scene. */
#pragma once
#include "hk.h"

/* ---------------------------------------------------------------- PlayerData (what a save keeps) */
#define MAX_PERSIST 1024
#define SCENE_NAME 32
/* the game's many bools (PlayerData's), by number: new ones only ever added at the end (saves keep them) */
enum {
  PDF_AT_BENCH, PDF_HAS_MAP, PDF_HAS_QUILL, PDF_HAS_CHARM, PDF_CHARM_BENCH_MSG, PDF_MET_ELDERBUG, PDF_VISITED_CROSSROADS,
  PDF_FALSE_KNIGHT_DEFEATED, PDF_FK_FIRST_PLOP, PDF_MAPPER_SHOP, PDF_CORN_CROSSROADS_LEFT, PDF_HORNET1_DEFEATED,
  PDF_VISITED_DIRTMOUTH, PDF_TISO_ENCOUNTERED_TOWN, PDF_HAS_DASH, PDF_SHAMAN_PILLAR, PDF_VISITED_GREENPATH,
  PDF_ELDERBUG_HISTORY1, PDF_ELDERBUG_SPEECH_SLY, PDF_ELDERBUG_SPEECH_STATION, PDF_ELDERBUG_SPEECH_EGG_TEMPLE,
  PDF_ELDERBUG_SPEECH_MAP_SHOP, PDF_ELDERBUG_FIRST_CALL, PDF_SLY_RESCUED, PDF_OPENED_TOWN_BUILDING,
  PDF_EGG_TEMPLE_VISITED, PDF_OPENED_MAPPER_SHOP, PDF_DREAMER_SCENE1, PDF_CORN_GREENPATH_LEFT,
  PDF_HORNET_F19, PDF_MET_QUIRREL, PDF_TISO_ENCOUNTERED_BENCH, PDF_FOUND_TRINKET1, PDF_FOUND_TRINKET2,
  PDF_FOUND_TRINKET3, PDF_FOUND_TRINKET4, PDF_HAS_CITY_KEY,
  /* (gotCharm_1 .. 40, equippedCharm_1 .. 40: the charm numbers the game's) */
  PDF_GOT_CHARM_1, PDF_GOT_CHARM_2, PDF_GOT_CHARM_3, PDF_GOT_CHARM_4, PDF_GOT_CHARM_5, PDF_GOT_CHARM_6,
  PDF_GOT_CHARM_7, PDF_GOT_CHARM_8, PDF_GOT_CHARM_9, PDF_GOT_CHARM_10, PDF_GOT_CHARM_11, PDF_GOT_CHARM_12,
  PDF_GOT_CHARM_13, PDF_GOT_CHARM_14, PDF_GOT_CHARM_15, PDF_GOT_CHARM_16, PDF_GOT_CHARM_17, PDF_GOT_CHARM_18,
  PDF_GOT_CHARM_19, PDF_GOT_CHARM_20, PDF_GOT_CHARM_21, PDF_GOT_CHARM_22, PDF_GOT_CHARM_23, PDF_GOT_CHARM_24,
  PDF_GOT_CHARM_25, PDF_GOT_CHARM_26, PDF_GOT_CHARM_27, PDF_GOT_CHARM_28, PDF_GOT_CHARM_29, PDF_GOT_CHARM_30,
  PDF_GOT_CHARM_31, PDF_GOT_CHARM_32, PDF_GOT_CHARM_33, PDF_GOT_CHARM_34, PDF_GOT_CHARM_35, PDF_GOT_CHARM_36,
  PDF_GOT_CHARM_37, PDF_GOT_CHARM_38, PDF_GOT_CHARM_39, PDF_GOT_CHARM_40,
  PDF_EQUIPPED_CHARM_1, PDF_EQUIPPED_CHARM_2, PDF_EQUIPPED_CHARM_3, PDF_EQUIPPED_CHARM_4, PDF_EQUIPPED_CHARM_5,
  PDF_EQUIPPED_CHARM_6, PDF_EQUIPPED_CHARM_7, PDF_EQUIPPED_CHARM_8, PDF_EQUIPPED_CHARM_9, PDF_EQUIPPED_CHARM_10,
  PDF_EQUIPPED_CHARM_11, PDF_EQUIPPED_CHARM_12, PDF_EQUIPPED_CHARM_13, PDF_EQUIPPED_CHARM_14, PDF_EQUIPPED_CHARM_15,
  PDF_EQUIPPED_CHARM_16, PDF_EQUIPPED_CHARM_17, PDF_EQUIPPED_CHARM_18, PDF_EQUIPPED_CHARM_19, PDF_EQUIPPED_CHARM_20,
  PDF_EQUIPPED_CHARM_21, PDF_EQUIPPED_CHARM_22, PDF_EQUIPPED_CHARM_23, PDF_EQUIPPED_CHARM_24, PDF_EQUIPPED_CHARM_25,
  PDF_EQUIPPED_CHARM_26, PDF_EQUIPPED_CHARM_27, PDF_EQUIPPED_CHARM_28, PDF_EQUIPPED_CHARM_29, PDF_EQUIPPED_CHARM_30,
  PDF_EQUIPPED_CHARM_31, PDF_EQUIPPED_CHARM_32, PDF_EQUIPPED_CHARM_33, PDF_EQUIPPED_CHARM_34, PDF_EQUIPPED_CHARM_35,
  PDF_EQUIPPED_CHARM_36, PDF_EQUIPPED_CHARM_37, PDF_EQUIPPED_CHARM_38, PDF_EQUIPPED_CHARM_39, PDF_EQUIPPED_CHARM_40,
  PDF_OVERCHARMED, PDF_CAN_OVERCHARM, PDF_HAS_LANTERN, PDF_HEART_PIECE_COLLECTED, PDF_VESSEL_FRAGMENT_COLLECTED,
  PDF_MET_SLY_SHOP, PDF_MET_ISELDA, PDF_MET_CHARM_SLUG, PDF_SLY_SHELL_FRAG1, PDF_SLY_SHELL_FRAG2, PDF_SLY_VESSEL_FRAG1,
  PDF_SLY_SIMPLE_KEY, PDF_SLY_RANCID_EGG, PDF_MAP_CROSSROADS, PDF_MAP_GREENPATH, PDF_HAS_PIN, PDF_HAS_PIN_BENCH,
  PDF_HAS_PIN_COCOON, PDF_HAS_PIN_SHOP, PDF_HAS_PIN_SPA, PDF_HAS_PIN_STAG, PDF_HAS_MARKER, PDF_HAS_MARKER_B,
  PDF_HAS_MARKER_R, PDF_HAS_MARKER_Y, PDF_HAS_MARKER_W, PDF_SALUBRA_NOTCH1, PDF_SALUBRA_NOTCH2, PDF_SALUBRA_NOTCH3,
  PDF_SALUBRA_NOTCH4, PDF_MET_STAG, PDF_CORNIFER_AT_HOME, PDF_ISELDA_CONVO1, PDF_ISELDA_CORNIFER_HOME_CONVO,
  PDF_SALUBRA_CONVO_COMBO, PDF_SALUBRA_CONVO_OVERCHARM, PDF_SALUBRA_CONVO_TRUTH, PDF_SLY_CONVO_MAPPER,
  PDF_OPENED_CROSSROADS, PDF_OPENED_GREENPATH, PDF_TRAVELLING, PDF_STAG_REMEMBER1, PDF_STAG_CONVO_TISO,
  PDF_QUIRREL_LEFT_EGG_TEMPLE, PDF_MET_CORNIFER, PDF_CORN_CROSSROADS_ENCOUNTERED, PDF_CORNIFER_INTRODUCED, PDF_MENDER_SIGN_BROKEN,
  PDF_FALSE_KNIGHT_WALL_REPAIRED, PDF_FALSE_KNIGHT_WALL_BROKEN, PDF_COUNT
};
typedef struct {
  /* (saved: the layout only ever grows into reserved) */
  int8_t health, max_health, health_blue, nail_damage;
  int16_t mp, mp_reserve, max_mp, mp_reserve_max;
  int32_t geo;
  uint8_t fireball_level, charm_slots, respawn_type;
  bool can_dash, has_dash, has_spell, respawn_facing_right, soul_limited;
  char respawn_scene[SCENE_NAME], respawn_marker[SCENE_NAME];
  /* the shade: where the Knight died, what it keeps */
  char shade_scene[SCENE_NAME];
  float shade_x, shade_y;
  int8_t shade_health, shade_fireball_level;
  int16_t shade_mp;
  int32_t geo_pool;
  float play_time;
  uint8_t flags[32];      /* PDF_* */
  uint8_t shaman;         /* (the Snail Shaman's state) */
  uint8_t current_area;   /* (the area whose title showed last: AreaTitleController) */
  uint8_t elderbug;       /* (Elderbug's state) */
  uint8_t hornet_greenpath;   /* (hornetGreenpath: her encounters in Greenpath) */
  uint8_t quirrel_egg_temple;   /* (quirrelEggTemple: his talks at the Black Egg) */
  uint8_t map_zone;       /* (mapZone: the room's, as it is entered; the save profiles show its area) */
  uint8_t charms_owned;   /* (charmsOwned) */
  uint8_t trinkets[4];    /* (trinket1 .. 4: Wanderer's Journals, Hallownest Seals, King's Idols, Arcane Eggs) */
  uint8_t rancid_eggs, ore;
  uint8_t charm_slots_filled;   /* (charmSlotsFilled: the notches the equipped charms take) */
  uint8_t heart_pieces, vessel_fragments, simple_keys;
  uint8_t grubs_collected, grub_rewards;   /* (grubsCollected, grubRewards: the Grubfather's given) */
  uint8_t equipped[12];         /* (equippedCharms: in the order they were equipped; 0 ends it) */
  uint8_t current_inv_pane;     /* (currentInvPane) */
  uint8_t stag_position1;       /* (stagPosition + 1: 0 none yet, 1 Dirtmouth, 2 Crossroads, 3 Greenpath) */
  uint8_t stations_opened;      /* (stationsOpened) */
  /* (scenesVisited, scenesMapped (the quill's), scenesEncounteredCocoon (a lifeblood cocoon broken there): by room) */
  uint8_t rooms_visited[5], rooms_mapped[5], rooms_cocoon[5];
  uint8_t map_key_pref;         /* (mapKeyPref: the map's key and its pins shown 0, the pins only 1, neither 2) */
  uint8_t mender_state;         /* (menderState: 1 the sign broken once, 2 the Mender Bug killed) */
  uint8_t reserved[13];
  /* (save version 2 on) the markers placed on the map (placedMarkers_b, _r, _y, _w: their places, 1/100 map units) */
  uint8_t markers_placed[4];
  int16_t markers[4][6][2];
  /* (saved apart, by the objects' names: their states) */
  uint8_t persist[MAX_PERSIST / 8];
  /* (not saved) */
  bool disable_pause;
} PlayerData;
#define PD_SAVED offsetof(PlayerData, persist)
#define PD_SAVED_V1 offsetof(PlayerData, markers_placed)   /* (a version 1 save's: no markers) */
extern PlayerData g_pd;
static inline bool pd_flag(int f) { return g_pd.flags[f >> 3] >> (f & 7) & 1; }
/* (equippedCharm_N) */
static inline bool charm_on(int id) { return pd_flag(PDF_EQUIPPED_CHARM_1 + id - 1); }
static inline void pd_set_flag(int f, bool on) {
  if (on) g_pd.flags[f >> 3] |= (uint8_t)(1 << (f & 7));
  else g_pd.flags[f >> 3] &= (uint8_t)~(1 << (f & 7));
}
/* the saves (save.c): GameManager.SaveGame, LoadGame; a slot's file */
#define SAVE_SLOTS 4
bool save_game(void);
bool save_load(int slot);
bool save_exists(int slot);
void save_select(int slot);
/* (SaveStats: what the save profiles show of a slot's game) */
typedef struct {
  int8_t max_health;
  uint8_t zone;
  int16_t mp_reserve_max;
  int32_t geo;
  float play_time;
} SaveStats;
int save_stats(int slot, SaveStats *st);   /* -> 0 no game, 1 a game, -1 a file that does not check out */
bool save_clear(int slot);
void save_restore(void);   /* (the saves gone: from their copy, hollowknight_saves.py) */
void save_set_respawn(const char *marker, bool facing_right);   /* (Bench Control's Rest Burst) */
void save_set_respawn_kind(const char *marker, bool facing_right, int type);   /* (respawnType: 0 face down) */

/* ---------------------------------------------------------------- the room's game objects (tools/ents.py) */
enum { ENT_CAMLOCK = 1, ENT_GATE, ENT_HAZARD_MARKER, ENT_RESPAWN, ENT_HAZARD_TRIGGER, ENT_MASK, ENT_DAMAGE, ENT_SHAPE,
       ENT_BOX, ENT_OBJ, ENT_PIECE, ENT_SHADE_MARKER };   /* (shape, box, piece: more of the record before) */
enum { OK_BREAKABLE = 1, OK_ENEMY, OK_GREAT_DOOR, OK_GEO_ROCK, OK_CHEST, OK_BENCH, OK_BATTLE, OK_FK_FLOOR, OK_BGATE,
       OK_ARENA, OK_EVENT, OK_SUMMON, OK_COND, OK_PROP, OK_DRIP, OK_COCOON, OK_AREA };   /* objects (ENT_OBJ's flags) */
/* an enemy record's s1: starts alert (or first); startles; one of an arena's Pre Battle Enemies; its death counts for its arena; gone once its arena's
 * fight is over; spawned by its mother's burster; there only once its arena's fight is over; its FSMs off till near
 * the camera (FSMActivator); then its death's effects
 * (EF_DEATH_SHIFT: enemyDeathType, and 12-13: which EnemyDeathEffects) */
enum { EF_START = 1, EF_STARTLES = 2, EF_PREBATTLE = 4, EF_BATTLE = 8, EF_ARENA_GONE = 16, EF_SPAWNED = 32, EF_ARENA_LATER = 64,
       EF_DORMANT = 128 };
#define EF_DEATH_SHIFT 8
#define EF_CONTACT 0x4000   /* (off till the Knight touches its parent's trigger: ActivateChildrenOnContact) */
/* battle gates (BG Control: their events) and arenas (Battle Control: obj.c) */
enum { BG_CLOSE, BG_QUICK_CLOSE, BG_OPEN, BG_QUICK_OPEN, BG_DESTROY };
void gates_event(int ev);
int gate_find(uint16_t name);    /* (a gate by its name: the scripts' strings) */
void gate_event_at(int k, int ev);
void gate_off(int k);   /* (ActivateGameObject false: gone) */
int camlock_find(uint16_t name);  /* (a camera lock area, a mask, by its name: the scripts' strings) */
void arena_start(void);          /* START (sent by an enemy) */
void arena_enemy_died(void);     /* (one it counts) */
void arena_set_activated(void);  /* Activated: its fight over (and saved) */
bool arena_done(void);
void enemies_summon(void);       /* SUMMON: the summoners bring their enemies */
void enemies_battle_start(void); /* BATTLE START */
void enemies_hornet_wake(void);  /* WAKE (to Hornet) */
void enemies_hornet_saver(bool on);   /* her Hornet Saver's walls (ActivateAllChildren) */
enum { HB_BOUNCE = 1, HB_RECOIL = 2 };    /* a hit box (ENT_BOX's flags): a down slash bounces off it, a slash recoils */
enum { HAZ_NONE, HAZ_NORMAL, HAZ_SPIKES, HAZ_ACID, HAZ_LAVA, HAZ_PIT };   /* DamageHero.hazardType */
enum { MK_SECRET = 1, MK_REMASK = 2, MK_SIMPLE = 4 };   /* masks: the unmasker, remasker and inverse FSMs */
enum { CL_PREVENT_UP = 1, CL_PREVENT_DOWN = 2, CL_MAX_PRIORITY = 4 };
enum { G_DOOR = 1, G_ENTER_RIGHT = 2, G_ENTER_LEFT = 4, G_DONT_WALK_OUT = 8, G_NON_HAZARD = 16, G_HARD_LAND = 32,
       G_ENTRY_ONLY = 64 };   /* (entry only: its collider off, never left by) */
#define FACING_RIGHT 1
typedef struct {
  uint8_t type, flags;
  uint8_t group, group2;   /* its render groups */
  uint16_t a;              /* gate: the room it leads to; hazard trigger: its marker */
  uint16_t persist;        /* its bit in the save (NO_PERSIST: none) */
  float x0, y0, x1, y1;    /* its trigger (or its place) */
  float p0, p1, p2, p3;    /* camera lock: x min, y min, x max, y max (-1: none); gate: entry delay; mask: fade time,
                              pause, then the trigger kind (simple) or the alphas of Idle and Fade Out (remasker) */
  uint16_t s0, s1;         /* names (str_at): a gate's own and its entry point's */
} Ent;
#define NO_PERSIST 0xFFFF
void world_enter(void);                     /* the room's objects, as the room starts */
void world_tick(void);                      /* 1/50 s */
void world_trigger(int ent, int kind);      /* the Knight entering, staying in, leaving an object's trigger (EV_*) */
void world_hero_in_position(void);          /* the Knight done entering the room (WaitForHeroInPosition) */
bool world_hero_placed(void);               /* (he is: since the room loaded) */
void world_send_hit(int ent);               /* HIT to an object's FSM (from another's) */
void group_fade(int group, float alpha, float time);   /* iTweenFadeTo (linear) */
bool persist_get(int bit);   /* an object's state in the save (PersistentBoolItem) */
void persist_set(int bit);
void persist_clear(int bit);

/* the objects the Knight acts on (obj.c) */
void obj_enter(void);
void obj_tick(void);
void obj_draw(void);
void obj_keep_cocoons(void);
void obj_swing_start(void);                                  /* a slash's hit shape on: what it hits, once */
int obj_nail(const float *pts, int npts, float direction);   /* -> HB_* of what it touches */
float rand_range(float lo, float hi);   /* Random.Range (floats) */

/* enemies, their corpses, geo (enemy.c) */
void enemies_enter(void);
void enemies_fixed(void);    /* FixedUpdate and their physics */
void enemies_update(void);   /* Update */
void enemies_draw(void);
void enemies_swing_start(void);
void enemies_hero_cast_spell(void);   /* (HERO CAST SPELL) */
int enemies_nail(const float *pts, int npts, float direction, int damage);
int enemies_touch_hero(float x0, float y0, float x1, float y1, int *side);   /* -> its damage, 0: none */
void enemies_hero_leave(void);   /* HERO LEAVE (the Knight dead): the shade departs */
/* geo of a size (0 small, 1 medium, 2 large) flung from (x, y), each from a little about it (FlingUtils) */
void body_bounce(Body *b, float pvx, float pvy, int had, float factor);   /* ObjectBounce */
void geo_fling_at(int type, int n, float x, float y, float smin, float smax, float amin, float amax, float spread);
int cardinal(float degrees);            /* DirectionUtils.GetCardinalDirection */
const Ent *room_ents(int *n);
#define MAX_ENTS 448

/* ---------------------------------------------------------------- the game */
typedef struct {
  bool paused;
  float time;
  float time_scale;           /* (Time.timeScale: freezes when the Knight is hit) */
  float step_acc;
  float hazard_x, hazard_y;   /* where a hazard sends the Knight back to */
  bool hazard_facing_right;
  /* GameManager's coroutines */
  uint8_t freeze_phase;
  bool freeze_hero;
  float freeze_t, freeze_from, freeze_down, freeze_wait, freeze_up, freeze_target;
  uint8_t hazard_phase;
  float hazard_t;
  /* a scene transition */
  uint8_t scene_phase;
  int16_t next_room;
  uint16_t next_entry;
  float scene_t, next_delay;
  /* the camera's fade to black */
  float fade, fade_from, fade_to, fade_t, fade_time, fade_delay;
} Game;
void game_freeze_moment(void);         /* the Knight hit: FreezeMoment with HeroController's DAMAGE_FREEZE_* */
void game_freeze(float down, float wait, float up, float target, bool hero);   /* GameManager.FreezeMoment */
#define FREEZE_MOMENT_1() game_freeze(0.04f, 0.03f, 0.04f, 0, false)              /* FreezeMoment(1): a kill */
void game_player_dead_from_hazard(void);
void game_fade(float to, float time, float delay);
void game_fade_scene_in(void);   /* the camera's FADE SCENE IN */
/* BeginSceneTransition: to the room's entry gate (a string), leaving by a gate kind (GATE_UNKNOWN: standing),
 * entering after delay; without input: the Knight comes in without control */
void game_transition(int room, int entry, int gate, float delay, bool without_input);
extern Game g_game;

/* ---------------------------------------------------------------- the Knight (hero.c) */
enum { HS_INACTIVE, HS_IDLE, HS_RUNNING, HS_AIRBORNE, HS_WALL_SLIDING, HS_HARD_LANDING, HS_DASH_LANDING, HS_NO_INPUT,
       HS_PREVIOUS, HS_GROUNDED };   /* ActorStates (grounded and previous only as SetState's arguments) */
enum { TS_WAITING_TO_TRANSITION, TS_EXITING_SCENE, TS_WAITING_TO_ENTER_LEVEL, TS_ENTERING_SCENE, TS_DROPPING_DOWN };
enum { GATE_TOP, GATE_RIGHT, GATE_LEFT, GATE_BOTTOM, GATE_DOOR, GATE_UNKNOWN };
enum { SIDE_TOP, SIDE_LEFT, SIDE_RIGHT, SIDE_BOTTOM };
enum { ATK_NORMAL, ATK_UP, ATK_DOWN };
enum { SLASH_NORMAL, SLASH_ALT, SLASH_UP, SLASH_DOWN };
enum { DAMAGE_FULL, DAMAGE_HAZARD_ONLY, DAMAGE_NONE };
#define RECOIL_DURATION 0.2f
#define RECOIL_DURATION_STAL 0.08f   /* (Stalwart Shell) */

typedef struct {   /* HeroControllerStates */
  bool facing_right, on_ground, jumping, dashing, falling, attacking, up_attacking, down_attacking, alt_attack;
  bool looking_up, looking_down, looking_up_anim, looking_down_anim, bouncing, recoiling_left, recoiling_right;
  bool recoiling, recoil_frozen, dead, hazard_death, hazard_respawning, will_hard_land, casting, cast_recoiling;
  bool prevent_dash, dash_cooldown, in_walk_zone, touching_wall, touching_non_slider, was_on_ground, transitioning;
  bool invulnerable, focusing;
} CState;

typedef struct {
  Body body;
  uint32_t keys, prev_keys;
  uint8_t state, prev_state, anim_state, transition_state, gate_position, damage_mode;
  float move_input, vertical_input;
  CState cs;
  int jump_steps, jumped_steps, jump_queue_steps, dash_queue_steps, attack_queue_steps, recoil_steps;
  int landing_buffer_steps, ledge_buffer_steps, head_bump_steps;
  bool jump_queuing, dash_queuing, attack_queuing, recoil_large, hard_landed, air_dashed;
  float dash_timer, dash_cooldown_timer, attack_time, attack_duration, attack_cooldown, alt_attack_time;
  float time_since_level, bounce_timer, recoil_timer, recoil_vx, recoil_vy, fall_timer, hard_landing_timer;
  float dash_landing_timer, hard_land_fail_safe_timer, floating_buffer_timer, look_delay_timer, prev_gravity;
  float transition_vx, transition_vy;
  bool accepting_input, control_relinquished, doing_hazard_respawn, landed_event;
  bool touching_wall_l, touching_wall_r;
  bool hidden, hit_buffered, enter_without_input;
  float prevent_cast;   /* (PreventCastByDialogueEnd: casts kept back a moment) */
  float parry_t;        /* (NailParry: unhurt by enemies a moment) */
  int16_t entry_gate;   /* the record of the gate it came in by (-1: none) */
  int8_t buffered_side, buffered_damage, buffered_hazard;
  float invuln_freeze, invuln_time, pulse_t, recoil_timer2, respawn_timer, wake_timer;
  bool invuln_routine, pulsing, pulse_reverse, respawning;
  int8_t thunk_dir;
  bool thunk_hit;
  bool fury;            /* (Charm Effects' Fury: on at one mask, with Fury of the Fallen) */
  float thunk_timer;
  /* HeroAnimationController */
  Anim anim;
  bool anim_control, play_landing, play_run_to_idle, play_dash_to_idle, complete_delegate, set_entry_anim;
  bool was_facing_right, was_attacking;
} Hero;
extern Hero g_hero;

void hero_init(float x, float y, bool facing_right);
void hero_fixed(uint32_t keys);   /* a 1/50 s step: FixedUpdate, the physics and its collisions */
void hero_update(void);           /* then Update and the animations */
void hero_draw(void);
bool hero_touching_ground(void);
void hero_recoil_left(void);
void hero_nail_parry(void);   /* (NailParry) */
void hero_recoil_right(void);
void hero_recoil_down(void);
void hero_bounce(void);
void hero_finished_entering_scene(bool set_hazard_marker);
void hero_leave_scene(int gate);   /* LeaveScene: walking, jumping or falling out through the gate */
/* EnterScene: the Knight through the room's entry gate (its record, kind, place, entry offset, G_* flags) after delay */
void hero_enter_scene(int gate_ent, int gate, float gx, float gy, float ox, float oy, uint8_t flags, float delay);
void hero_add_blue_health(void);   /* (a lifeblood mask more) */
void hero_take_damage(int side, int damage, int hazard);   /* (side: where the damage comes from, SIDE_LEFT/RIGHT) */
void hero_recoil_unfreeze(void);       /* the end of StartRecoil, after the freeze */
void hero_hazard_respawn(void);        /* HeroController.HazardRespawn */
void hero_check_damage(void);          /* HeroBox: the hazards and enemies touching it */
void hero_late_update(void);           /* (HeroBox.LateUpdate: a buffered hit) */
void hero_soul_gain(void);             /* a nail's hit on an enemy */
void hero_add_health(int amount);
void hero_relinquish_control(void);    /* RelinquishControl, RegainControl */
void hero_relinquish_control_not_velocity(void);
void hero_charm_update(void);          /* CharmUpdate */
void hero_regain_control(void);
void hero_stop_anim_control(void);     /* StopAnimationControl, StartAnimationControl */
void hero_start_anim_control(void);
bool hero_can_focus(void);
bool hero_can_cast(void);
bool hero_can_quick_map(void);         /* CanQuickMap */
void hero_face(bool right);           /* FaceRight, FaceLeft */
void hero_gravity(bool on);           /* AffectedByGravity */
void hero_max_health(void);           /* MaxHealth */
float hero_ground_y(float x, float y);   /* FindGroundPoint */
void hero_wake_up_ground(void);

/* the Knight's death (death.c): his Hero Death object, GameManager.PlayerDead, then the respawn */
void death_start(float x, float y, bool facing_right);
void death_reset(void);
void death_tick(void);
void death_draw(void);
void shade_spawn_check(void);   /* (SceneManager: the shade where the Knight died, if this is that room) */

/* the map (map.c): the Knight's Map Control, the quick map, the rooms visited and mapped */
void map_reset(void);
void map_tick(void);          /* (the Knight's Map Control: with his update) */
void map_cancel(void);        /* (HERO DAMAGED, LEAVING SCENE, FSM CANCEL) */
void map_hud_tick(void);
void map_draw(void);
bool map_shown(void);
void map_room_entered(void);  /* (scenesVisited) */
void map_scene_visited(int r);   /* (AddToScenesVisited: a map piece of its own, after the rooms) */
void map_cocoon_broken(void); /* (AddToCocoonList) */
bool map_update(void);        /* UpdateGameMap: the quill's -> any room newly mapped */
void map_quick(bool open);    /* (a bench's: OPEN QUICK MAP, CLOSE QUICK MAP) */
void map_msg_show(void);      /* (Map Update Msg) */

/* benches (npc.c) */
void benches_enter(void);
void benches_tick(void);
/* the titles (title.c): an area's, a boss's or a character's (TITLE_*), as Area Title's FSM variables are set:
 * Visited (the small title), NPC Title (small, till NPC TITLE DOWN or NPC CONVO START), Display Right */
enum { TF_VISITED = 1, TF_NPC = 2, TF_RIGHT = 4 };
/* (AreaTitleController: OK_AREA's p3, tools/ents.py) */
enum { AF_TRIGGER = 1, AF_REVISIT = 2, AF_RIGHT = 4, AF_DOOR = 8, AF_SUB = 16, AF_AFTER_CROSSROADS = 32 };
void title_show(int title, int flags);
void title_npc_convo_start(void);
void title_npc_down(void);
void titles_enter(void);
void titles_hero_in_position(void);
void titles_tick(void);
void titles_draw(void);
/* the game's own scripts (vm.c): a room's, run each step, their objects drawn; events heard by all of them */
void hero_add_mp_charge(int amount);   /* (AddMPCharge) */
void hero_cancel_hero_jump(void);      /* (CancelHeroJump) */
void vm_enter(void);
void vm_tick(void);
void vm_draw(void);
void vm_broadcast(int ev);
void vm_send(int obj, int ev);
void vm_reset_semi(void);
void persist_reset_semi(void);   /* (ResetSemiPersistentItems: at a bench, on death) */   /* (an object's FSMs) */
void vm_fsm_set(int fsm, int var, uint32_t v, int ev);   /* (an FSM's variable set, then an event to it) */
/* the stag's menu and its rides (stag.c): Open Stag's menu for an FSM (its choice to that variable, then CONTINUE);
 * nextScene; Cinematic_Stag_travel (the screen black, then the new room's door_stagExit) */
void stag_reset(void);
void stag_menu_open(int fsm, int var);
void stag_next_scene(int str);
void stag_travel(void);
uint32_t stag_tick(uint32_t keys);   /* -> the keys the game has (none while the menu is open) */
bool stag_busy(void);
void vm_spell(float x0, float y0, float x1, float y1, float direction);   /* (a spell's box: the triggers it is in) */
int vm_nail(const float *pts, int npts, float direction);   /* (the nail's slash: what it meets; -> HB_*) */
void soul_orbs_fling(int n, float x, float y, float smin, float smax, float amin, float amax, float vx, float vy);
void piece_spawn(int sprite, int layer, int order, int flags, float x, float y, float z, float rot, float gravity,
                 float bounce, float spin_factor, float mirror, float vx, float vy, bool resting);   /* (obj.c: debris) */
void vm_activate_children(uint16_t name, bool on);       /* ActivateAllChildren of a script object, by name */
/* the message as an item is taken (msg.c: MSG_*), the HUD Blanker (a white screen the scripts fade) */
void msg_show(int item);
bool msg_shown(void);
void msg_tick(void);
void msg_draw(void);
void blanker_set(float alpha, bool on);
void white_blanker_fade(bool in);   /* HUD Blanker White: FADE IN, FADE OUT (over its Fade Time) */
void white_blanker_time(float t);
void white_blanker_reset(void);    /* (and the Prompt Blanker, the focus tutorial) */
void prompt_blanker(bool up);       /* the HUD's Prompt Blanker: UP, DOWN */
void focus_prompt_fade(bool up);    /* the focus tutorial (Focus_prompt_temp): its ColorFaders' Fade */
void first_map_prompt(bool up);     /* the first map's lesson (First Map): FIRST MAP UP, DOWN */
/* the notices (Relic Get Msg, Charm Get Msg) and the charm tutorial (Charm Tutorial Msg) */
void notice_icon(int k);     /* (the next notice's icon: the scripts' NOTICE_ICONS) */
void notice_show(int text);  /* a relic's notice: its icon, its name */
void charm_notice(int id);   /* a charm's (in the tutorial, as it is made: its charm) */
void charm_tute(void);
void charm_tute_input(void);
void charm_tute_close(void);
void notices_close(void);   /* (DESTROY JOURNAL MSG: the inventory opens)*/
bool bench_respawn(const char *name);   /* RESPAWN: the Knight asleep on the bench so named */

/* the Spell Control FSM (spell.c): focus, spells */
void spell_reset(void);
void spell_update(void);
void spell_cancel(void);
void spell_cancel_regain(void);   /* (HERO DAMAGED, LEAVING SCENE) */
bool spell_busy(void);
void spell_cast(bool up, bool down);
void fireballs_tick(void);
void fireballs_draw(void);
/* a spell's box (world): the enemies it touches hit (but those in done: bits of the enemies) -> those it touched */
uint32_t enemies_spell(float x0, float y0, float x1, float y1, float direction, int damage, float magnitude, uint32_t done);
void obj_spell(float x0, float y0, float x1, float y1, float direction, uint32_t *done);
void hero_add_geo(int amount);

/* the nail's slashes (NailSlash), children of the Knight */
void slash_start(int kind);
void slash_cancel(void);
void slash_tick(void);
void slash_draw(void);

/* effects */
void fx_dash_burst(float x, float y, bool facing_right, bool on_ground);

void game_new(void);
bool game_enter(int room, float x, float y, bool facing_right);
bool game_enter_gate(int room, const char *gate);   /* (a new game: through its entry gate) */
bool game_respawn(void);   /* the Knight at the save's respawn point (a loaded game, after dying) */
void game_tick(uint32_t keys);   /* 1/50 s */
void game_draw(void);
void game_draw_layers(void);   /* (what the game draws, before the frame is made) */
bool game_changing_room(void);
/* the title screen, the save profiles, the pause menu (menu.c) */
void menu_start(void);
bool menu_tick(uint32_t keys);   /* -> the game kept from ticking (a menu is up) */
void menu_draw(void);
bool menu_in_game(void);
/* the menus' pieces (inv.c): a sprite at its place and scale, a text's place (x its left, center or right as it is
 * aligned); drawn over all (gfx_overlay), in HUD units from (ox, oy) */
typedef struct {
  int16_t sprite;
  float x, y, kx, ky;
} UiPiece;
typedef struct {
  float x, top, w;
} UiText;
void ui_piece(const UiPiece *p, float ox, float oy, float k, uint8_t tint);
void ui_sprite_at(int sprite, float x, float y, float k, uint8_t tint);
void ui_text_at(int text, int style, const UiText *t, float ox, float oy, int align, float a);   /* (0 left, 1 center, 2 right) */
void ui_number_at(int v, int style, float x, float top, int align, float a);
/* a mask shard's, a vessel fragment's getting and its UI (collect.c) */
void collect_reset(void);
void collect_start(int kind);   /* 0 a mask shard, 1 a vessel fragment */
void collect_tick(void);
void collect_draw(void);        /* (before the HUD: behind it) */
bool collect_active(void);
/* the shops' menu (shop.c): SHOP UP from its region's script; what it does the game goes on behind it */
void shop_reset(void);
void shop_event(int ev);
uint32_t shop_tick(uint32_t keys);   /* -> the keys the game has (none while it is open) */
bool shop_open(void);
/* the inventory (inv.c): opened with its key; the world goes on behind it */
void inv_reset(void);
uint32_t inv_tick(uint32_t keys);   /* -> the keys the game has (none while it is open) */
void inv_damage(void);          /* (HERO DAMAGED: it closes) */
bool inv_open(void);
bool inv_settle(void);        /* (Home: a charm on its way put in place) -> whether a save may be made */
float inv_behind(void);   /* (what of the world behind it shows: 1 closed) */
void inv_open_map(void);      /* (OPEN INVENTORY MAP: the map tapped twice) */
void inv_cursor_arrow(int which);   /* (the map pane's: on the left arrow -1, the right -2; else not shown) */
float ui_key(int name, float x, float y, uint8_t white, float a);   /* an action's key, by name -> its half width */
/* the inventory's map pane (map.c) */
enum { MAP_PANE_NONE, MAP_PANE_LEFT, MAP_PANE_RIGHT, MAP_PANE_CLOSE };
void map_pane_enter(bool shortcut);
int map_pane_tick(uint32_t keys, uint32_t pressed, uint32_t move, bool repeat);   /* -> MAP_PANE_* */
void map_pane_draw(float ox, float oy, float a, uint8_t white, bool current);
bool map_pane_holds(void);    /* (Do Not Close: zoomed into) */
float map_pane_nav(void);     /* (the pane's arrows' and the side panes' names' alpha: away as it is zoomed into) */
void map_pane_close(void);
float text_box(int text, int style, float x, float y, float w, int align, float a);
int text_fit(int text, float w, float room);   /* (a description's style: the first of MSG, MSG_S, MSG_XS that fits) */
int charm_cost(int id);         /* (charmCost_N) */
bool charm_equipped(int id);
bool menu_quit(void);

/* ---------------------------------------------------------------- text (text.c) */
#define TEXT_BR 10     /* (in a text: a line break, a page break) */
#define TEXT_PAGE 12
const uint8_t *text_get(int id);
float text_width(int style, const uint8_t *s, int n);
/* the dialogue box (DialogueManager): its BOX UP and BOX DOWN; a conversation in it (DialogueBox.StartConversation),
 * paged with the continue keys until CONVO_FINISH */
void dialogue_reset(void);
void dialogue_box_up(void);
void dialogue_box_down(void);
void dialogue_dream_box(bool up);   /* Box Open Dream: BOX UP DREAM, BOX DOWN DREAM */
void dialogue_centre(bool on);      /* (SetTextMeshProAlignment: the text centred, or to the left) */
void dialogue_place(int which, float y);   /* (SetPosition: the text's, the stop's, the arrow's (0, 1, 2), HUD units) */
void dialogue_stop_pause(float t);         /* (Dialogue Page Control's Stop Pause) */
void dialogue_start(int text);
/* the yes or no box (Box Open YN, Text YN): its question, its toll (TakeGeo on YES), who hears YES or NO (a VM object) */
void dialogue_box_up_yn(void);
void dialogue_start_yn(int text);
void dialogue_yn_toll(int cost);
void dialogue_yn_requester(int obj);
bool dialogue_finished(void);
bool dialogue_last_page(void);   /* (the text shown on its last page, or none shown) */
bool dialogue_box_shown(void);
void dialogue_cancel(void);
void dialogue_tick(void);
void dialogue_draw(void);
/* prompt markers (ShowPromptMarker, HidePromptMarker): a label at a place; handle: the one stored (-1: none) */
int prompt_show(int handle, int label, float x, float y);
void prompt_hide(int handle);
void prompts_reset(void);
void prompts_tick(void);
void prompts_draw(void);

/* ---------------------------------------------------------------- the HUD (hud.c) */
void hud_reset(void);
void hud_slide(bool out);   /* the Hud Canvas's Slide Out FSM: OUT, IN */
void hud_soul_limiter(bool up);   /* SOUL LIMITER UP, DOWN: the soul orb cracked or whole */
void hud_tick(void);
void hud_draw(void);

/* ---------------------------------------------------------------- the camera (camera.c) */
void cam_init(void);
void cam_tick(void);
#ifdef HOST
void cam_debug(void);
#endif
void cam_snap_to_hero(void);   /* (after a respawn) */
enum { SHAKE_ENEMY_KILL = 1, SHAKE_AVERAGE, SHAKE_BIG, SHAKE_SMALL };   /* the CameraShake FSM's events */
void cam_shake(int kind);
enum { RUMBLE_OFF, RUMBLE_SMALL, RUMBLE_MED, RUMBLE_BIG };
void cam_rumble(int kind);   /* (RumblingSmall, ...: on until turned off) */
void ent_set_enabled(int i, bool on);   /* (a trigger record on or off: its object (de)activated) */
void cam_freeze(void);         /* FreezeInPlace (both) */
void cam_lock(int ent);      /* CameraController.LockToArea, the hero entering the area's trigger */
void cam_release(int ent);   /* ReleaseLock */
