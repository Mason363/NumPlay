/* A vehicle and its world (car.h), as the game builds them: its PlayerCar classes' bodies, fixtures, joints and
 * driving, its terrain's edges, rope bridges, pits and trees, on Box2D 2.2.1 like the game. The numbers come from
 * the game's own code (Android libgame.so; see the README's credits). */
#include <math.h>
#include <new>
#include <string.h>
#include <Box2D/Collision/Shapes/b2CircleShape.h>
#include <Box2D/Collision/Shapes/b2EdgeShape.h>
#include <Box2D/Collision/Shapes/b2PolygonShape.h>
#include <Box2D/Collision/b2Collision.h>
#include <Box2D/Dynamics/Contacts/b2Contact.h>
#include <Box2D/Dynamics/Joints/b2DistanceJoint.h>
#include <Box2D/Dynamics/Joints/b2PrismaticJoint.h>
#include <Box2D/Dynamics/Joints/b2RevoluteJoint.h>
#include <Box2D/Dynamics/Joints/b2WheelJoint.h>
#include <Box2D/Dynamics/b2Body.h>
#include <Box2D/Dynamics/b2Fixture.h>
#include <Box2D/Dynamics/b2World.h>
#include "car.h"
#include "stage.h"
#include "vehicle.h"

#ifndef HOST
/* (no C++ runtime on the calculator: the few things Box2D's classes refer to; nothing is deleted) */
void operator delete(void *) noexcept {}
void operator delete(void *, unsigned int) noexcept {}
extern "C" void __cxa_pure_virtual(void) {
  for (;;) {
  }
}
#endif

namespace {

const float DT = 1.0f / CAR_HZ;

/* the game's object types (PhysicsObject::Type): what a body is, as the contacts tell */
enum { T_COIN = 2, T_FUEL = 4, T_CAR = 8, T_DRIVER = 32, T_GROUND = 64, T_BRIDGE = 128, T_TREE = 256, T_MUD = 512,
       T_LAVA = 1024 };
/* collision filters: category, mask */
const uint16 CAR_MASK = 0x7C6, DRIVER_MASK = 0x1C6, GROUND_MASK = 296, BRIDGE_MASK = 40, TREE_MASK = 120, PIT_MASK = 40;
const uint16 NONLETHAL = 0x300;   /* touching these with the head is fine (PlayerCar::getNonLethalObjectTypes: trees, mud) */

/* ------------------------------------------------------------------ the vehicles' numbers (PlayerCar*) */
struct Spec {
  float hz, damp0, damp_k;       /* suspension: spring frequency; damping ratio damp0 x damp_k^L */
  int damp_cap;                  /* the damping's level counts up to this (0: no cap) */
  float speed0, speed_k, speed_m;   /* top wheel speed speed0 x speed_k^L x speed_m (rad/s) */
  float turbo0, turbo_k;         /* the turbo's cap turbo0 x turbo_k^L */
  float rate0;                   /* the turbo's build rate rate0 x 1.1^(L-1) */
  float torque;                  /* the engine's torque (N m) */
  float tire0, tire_k;           /* the tires' friction tire0 x tire_k^L */
  float awd;                     /* the front wheel's share of the torque with no 4WD upgrade */
  float wy, r, rr, hwb;          /* wheels: below the spawn point (-), radius, rear radius, half wheelbase */
  float cdens, cfric, wdens;     /* chassis density and friction, wheels' density */
  float com_k;                   /* the centre of mass lowered by com_k a suspension level */
  float front_fric;              /* the front tyre's friction share */
  bool fork;                     /* a raked front fork (bikes) */
  float seat[2];                 /* Bill's seat from the spawn point */
  float neck_limit, driver_fric; /* his joints' bend, his friction */
  float air;                     /* the air control's torque */
  int nv;
  float poly[8][2];              /* the chassis */
  float down0, down_k;           /* downforce coefficient down0 + down_k x L (0, 0: none) */
};

/* indexes match vehicle.h (V_JEEP...V_TANK: the first eight; the others are built by their own code below) */
const Spec SPECS[V_TANK + 1] = {
  /* Hill Climber */
  {3, 0.2f, 1.11f, 14, 28, 1.05f, 1, 0.5f, 1.15f, 1.0f, 10, 0.5f, 1.115f, 0, 0, 0.4f, 0.4f, 1.15f, 1, 0.2f, 0.5f, 0.045f,
   1, false, {-0.1f, 0.8f}, 0.3f, 0.5f, 20, 6, {{-1, -0.5f}, {1, -0.5f}, {1.5f, 0}, {0, 0.5f}, {-1.15f, 0.5f}, {-1.5f, 0.2f}},
   0, 0},
  /* Motocross Bike */
  {3.5f, 0.2f, 1.11f, 14, 28, 1.05f, 1, 0.5f, 1.15f, 2.0f, 15, 1.85f, 1.115f, 0, -0.2f, 0.5f, 0.5f, 1.25f, 1, 1.5f, 0.6f,
   0.045f, 0.15f, true, {-0.3f, 1.35f}, 0.4f, 0.75f, 20, 6,
   {{-1.3f, 0.2f}, {-1.3f, -0.1f}, {-0.5f, -0.4f}, {0.5f, -0.4f}, {1.45f, -0.1f}, {1.45f, 0.2f}}, 0, 0},
  /* Monster Truck */
  {3, 0.2f, 1.11f, 0, 28, 1.05f, 0.55f, 0.5f, 1.15f, 2.5f, 20, 0.65f, 1.115f, 0, 0, 0.7f, 0.7f, 1.45f, 0.5f, 0.2f, 0.2f,
   0.05f, 1, false, {-0.1f, 0.8f}, 0.3f, 0.5f, 20, 6,
   {{-1.275f, -0.5f}, {1.275f, -0.5f}, {1.7f, 0}, {0, 0.5f}, {-1.3175f, 0.5f}, {-1.7f, 0.2f}}, 0, 0},
  /* Quad Bike */
  {3.25f, 0.2f, 1.11f, 14, 25, 1.05f, 1, 0.5f, 1.15f, 2.0f, 15, 1.85f, 1.115f, 0, -0.2f, 0.6f, 0.6f, 1.25f, 1, 1.5f, 0.3f,
   0.045f, 0.15f, true, {-0.3f, 1.35f}, 0.4f, 0.75f, 20, 6,
   {{-1.4f, 0.3f}, {-1.4f, -0.1f}, {-0.5f, -0.4f}, {0.5f, -0.4f}, {1.55f, -0.1f}, {1.55f, 0.3f}}, 0, 0},
  /* Race Car */
  {5.5f, 0.2f, 1.11f, 0, 28, 1.05f, 1, 0.5f, 1.15f, 2.5f, 20, 1.25f, 1.115f, 0, -0.4f, 0.5f, 0.625f, 1.45f, 1, 0.2f, 0.2f,
   0.045f, 1, false, {0.15f, 0.65f}, 0.3f, 0.5f, 20, 6,
   {{-1.875f, -0.5f}, {1.6875f, -0.4f}, {1.6875f, 0}, {0, 0.4f}, {-1.9375f, 0.55f}, {-2, 0.55f}}, 0.25f, 0.4f},
  /* Rally Car */
  {5, 0.2f, 1.11f, 0, 33, 1.05f, 1, 0.5f, 1.15f, 2.5f, 20, 1.25f, 1.115f, 0.5f, -0.25f, 0.5f, 0.5f, 1.3f, 1, 0.2f, 0.2f,
   0.045f, 1, false, {-0.25f, 0.9f}, 0.3f, 0.5f, 30, 6,
   {{-1.875f, -0.5f}, {1.6875f, -0.4f}, {1.6875f, 0}, {0, 0.4f}, {-1.9375f, 0.55f}, {-2, 0.55f}}, 0.25f, 0.3f},
  /* Dune Buggy */
  {4.5f, 0.2f, 1.11f, 0, 21, 1.05f, 1, 0.5f, 1.15f, 2.5f, 22, 1.35f, 1.115f, 0.5f, -0.35f, 0.5f, 0.625f, 1.6f, 1, 0.2f, 0.2f,
   0.045f, 1, false, {-0.15f, 0.85f}, 0.3f, 0.5f, 20, 6,
   {{-1.875f, -0.5f}, {1.6875f, -0.4f}, {1.6875f, 0}, {0, 0.4f}, {-1.9375f, 0.55f}, {-2, 0.55f}}, 0.25f, 0},
  /* Tank */
  {3, 0.3f, 1.0875f, 0, 34, 1.048f, 0.55f, 0.87f, 1.14f, 3.33f, 6, 0.5f, 1.1612f, 0.5f, 0, 0.35f, 0.35f, 1.45f, 0.5f, 0.2f,
   0.2f, 0.0707142857f, 1, false, {-0.2f, 1.4f}, 0.3f, 0.5f, 3.5f, 6,
   {{-1.275f, -0.4f}, {1.275f, -0.4f}, {2.04f, 0.5f}, {0, 1}, {-2.1675f, 0.5f}, {-2.04f, 0.2f}}, 0, 0},
};
/* the Tank's six wheels, rear to front: place from the spawn point, the joint's axis (not normalised, as in the
 * game), spring frequency (x the suspension's for 1 and 1.5), damping ratio */
const float TANK_WHEELS[6][6] = {
  {-2.2f, 1.0f, 0.8f, 0.2f, 10.1f, 1}, {-1.5f, 0.1f, 0, 1, -1, 1}, {-0.5f, -1.5e-17f, 0, 1, -1.5f, 1},
  {0.5f, -1.5e-17f, 0, 1, -1.5f, 1}, {1.5f, 0.1f, 0, 1, -1, 1}, {2.2f, 1.0f, -0.8f, 0.2f, 10.1f, 1},
};

/* The five vehicles of 1.10.2 the port had not had: their own classes' numbers (PlayerCarTractor, PlayerCarTouristbus,
 * PlayerCarDieselTruck, PlayerCarSnowMobile, PlayerCarTruck). Each one's bodies are made in create_new below. */
struct NewSpec {
  int nv;
  float poly[7][2];               /* the chassis */
  float cdens, com_k;             /* its density; the centre of mass lowered by com_k a level of upgrade com_up */
  int com_up;
  float hz, damp0, damp_k;        /* the wheels' springs: frequency, damping damp0 x damp_k^L(suspension) */
  float speed0, speed_k, speed_m; /* top wheel speed (rad/s) */
  float cap0, cap_k;              /* the turbo's cap cap0 x cap_k^L */
  float rate0;                    /* its build rate rate0 x 1.1^(L - rate_l) */
  int rate_l;
  float torque, awd, tire0, tire_k;
  float gas_r, gas_f, gas_k;      /* gas: a wheel's torque share T f (gas + gas_k turbo), rear and front */
  int reverse;                    /* 0: share T f^4.05 (0.5 + 0.5 t); 1: the bus' (rear f (0.3 + 0.7 t)); 2: the
                                     tractor's share T f (1 + t) / 2 */
  float brake, brake_air;         /* the brake's torque, on the ground and after 5 steps in the air */
  float decay0, decay1;           /* the turbo off gas: t -= dt (decay0 + decay1 t) */
  float air;                      /* the air control's torque */
  float seat[2];
  float weight;                   /* getObstacleWeightMultiplier */
  float mud_damp;
};
const NewSpec NSPECS[NVEHICLES - V_TRACTOR] = {
  /* Tractor */
  {7, {{1.5725f, -0.1895f}, {2.1215f, -0.0065f}, {2.213f, 0.5425f}, {2.1215f, 1.0915f}, {1.0235f, 1.22875f},
       {-2.0875f, 1.04575f}, {-2.22475f, 0.22225f}},
   0.65f, 0.035f, 3, 5, 1.35f, 1, 15, 1.05f, 1, 0.5f, 1.15f, 0.3f, 0, 20, 0.5f, 0.8f, 1.15f, 1, 1, 1, 2, 25, 1.25f,
   0.05f, 1.45f, 30, {-1.17578f, 2.013f}, 0.75f, 4},
  /* Tourist Bus */
  {4, {{-3.285f, 1}, {-3.285f, -0.5f}, {3.385f, -0.5f}, {3.785f, 1.5f}}, 0.55f, 0.045f, 1, 5, 0.2f, 1.11f, 15, 1.05f, 1,
   0.5f, 1.15f, 0.3f, 1, 10, 0.5f, 0.75f, 1.115f, 0.25f, 0.5f, 2, 1, 20, 1, 0.05f, 1.45f, 90, {2.55f, 2.55f}, 0.35f, 4},
  /* Super Diesel 4x4 */
  {5, {{-2.25f, 0.2f}, {-2.125f, -0.5f}, {2.1875f, -0.5f}, {2.3125f, 0.2f}, {1.25f, 0.8f}}, 0.775f, 0.045f, 1, 3, 0.2f,
   1.11f, 17, 1.05f, 1, 0.5f, 1.15f, 0.5f, 1, 20, 0.5f, 0.95f, 1.115f, 0.5f, 0.5f, 1.35f, 0, 20, 1, 0.01f, 0.75f, 45,
   {0.1f, 1.25f}, 0.65f, 3.2f},
  /* Snow Mobile (its pedals are the Tank's, over its five track wheels) */
  {6, {{-1.275f, -0.4f}, {2.21f, -0.6f}, {2.465f, -0.2f}, {0.68f, 1.1f}, {-2.04f, 0.25f}, {-2.04f, 0}}, 0.5f,
   0.0707142857f, 1, 3, 0, 0, 45.9f, 1.048f, 0.55f, 0.87f, 1.14f, 3.33f, 1, 6, 0.5f, 0.5f, 1.1612f, 0, 0, 0, 0, 20, 1,
   1, 4, 3.5f, {-0.2f, 1.4f}, 2, 4},
  /* Truck */
  {5, {{-2.875f, 0.2f}, {-2.875f, -0.5f}, {2.875f, -0.5f}, {2.9325f, 0.2f}, {1.15f, 1.1f}}, 0.95f, 0.045f, 1, 4, 0.2f,
   1.11f, 15, 1.05f, 1, 0.5f, 1.15f, 0.3f, 1, 10, 0.5f, 0.75f, 1.115f, 0.25f, 0.5f, 2, 1, 20, 1, 0.01f, 0.45f, 90,
   {0.85f, 1.65f}, 0.35f, 4},
};
/* the Snow Mobile's track wheels, from the spawn point: x, y, y's share of the suspension's damping (0.3), spring
 * frequency (or x the base's for negative), and the friction share it loses toward the front */
const float SNOW_TRACK[5][4] = {
  {-0.9f, 0.8f, 0, 10.1f}, {-2.1f, 0.1f, 0.3f, -2.5f}, {-0.5f, -0.2f, 0.3f, -1.47f}, {0.5f, 0.1f, 0.3f, -2.2f},
  {1.3f, 0.9f, 0, 30},
};

float powi(float b, int n) {
  float r = 1;
  for (int i = 0; i < n; i++) r *= b;
  return r;
}
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------------ the world */
alignas(8) unsigned char world_mem[sizeof(b2World)];
b2World *W;
b2Body *ground;

/* what a body is: its type, and which part of the car */
struct Tag {
  int type;
  int part;   /* 0 chassis, 1.. wheels (index + 1), -1 torso, -2 head, -3 none, -4 the exhaust, -5 a soft tire's ball,
                 -10 - 2 p a passenger's torso, -11 - 2 p their head */
};
Tag tag_ground = {T_GROUND, -3}, tag_bridge = {T_BRIDGE, -3}, tag_tree = {T_TREE, -3}, tag_mud = {T_MUD, -3},
    tag_lava = {T_LAVA, -3};
Tag tag_car[CAR_MAXW + 1], tag_torso = {T_CAR, -1}, tag_head = {T_CAR, -2}, tag_exhaust = {T_CAR, -4},
    tag_seg = {T_CAR, -5}, tag_pass[CAR_PASS * 2];

/* ------------------------------------------------------------------ the vehicle (PlayerCar) */
/* a rider (Bill or a bus passenger): torso, head, the neck's joint and the two springs */
struct Rider {
  b2Body *torso, *head;
  b2RevoluteJoint *neck;
  b2Joint *spring[2];
};

struct Vehicle {
  const Spec *s;                  /* (the first eight) */
  const NewSpec *n;               /* (the new five) */
  int veh, nw, ndriven;
  b2Body *chassis, *wheel[CAR_MAXW], *torso, *head;
  b2Joint *joint[CAR_MAXW];
  bool rev_joint[CAR_MAXW];       /* a revolute joint (the tractor's soft tire) rather than a wheel joint */
  b2RevoluteJoint *neck;          /* torso to head */
  b2DistanceJoint *spring[2];     /* chassis to torso, torso to head */
  float wr[CAR_MAXW], share[CAR_MAXW], gas[CAR_MAXW], wspeed[CAR_MAXW];
  float hz, damp, speed, turbo_cap, rate, tires, awd, air, down;
  float turbo, rev;
  int air_frames;
  unsigned touch_chassis, touch_wheel[CAR_MAXW], touch_exhaust;
  bool decel_air;
  bool dead, head_hit;
  int flips_k;                    /* the turn handleFlips counts from */
  bool alive_at_turn;
  bool accelerating;
  /* the new vehicles' parts */
  int level_com;                  /* (the tractor's stability) */
  int mud_a, mud_b;               /* the wheels whose touches PlayerCar::baseUpdate takes (its "chassis", "wheels") */
  Rider pass[CAR_PASS];
  int npass;
  bool pass_hit[CAR_PASS];
  b2Body *seg[CAR_SEGS];          /* the tractor's soft tire */
  int nseg;
  b2Body *exhaust;
  b2RevoluteJoint *exhaust_joint;
  /* mud and lava (PlayerCar::baseUpdate) */
  int mud_frames_a, mud_frames_b; /* steps the "chassis", anything, has been in mud */
  int burn[5];
} V;

/* the level of the vehicle's upgrade of a kind, or -1 */
int level_of(int veh, const uint8_t lv[4], int kind) {
  for (int u = 0; u < 4; u++)
    if (vehicles[veh].up[u] == kind) return lv[u];
  return -1;
}

b2Body *make_body(float x, float y, float angle, bool bullet, Tag *tag) {
  b2BodyDef bd;
  bd.type = b2_dynamicBody;
  bd.position.Set(x, y);
  bd.angle = angle;
  bd.bullet = bullet;
  bd.userData = tag;
  return W->CreateBody(&bd);
}

void add_fixture(b2Body *b, const b2Shape *sh, float friction, float density, uint16 cat, uint16 mask, int16 group) {
  b2FixtureDef fd;
  fd.shape = sh;
  fd.friction = friction;
  fd.density = density;
  fd.restitution = 0;
  fd.filter.categoryBits = cat;
  fd.filter.maskBits = mask;
  fd.filter.groupIndex = group;
  b->CreateFixture(&fd);
}

/* PlayerCar::createDriver: a torso and a head, each on a revolute joint and a spring (bus passengers alike) */
Rider create_rider(float x, float y, float limit, float friction, Tag *torso_tag, Tag *head_tag) {
  Rider r;
  b2Body *prev = V.chassis;
  for (int i = 0; i < 2; i++) {
    b2PolygonShape box;
    box.SetAsBox(0.3f, 0.3f);
    b2CircleShape circle;
    circle.m_radius = 0.33f;
    b2Body *b = make_body(x, y + 0.3f + 0.6f * (float)i, 3.14159f, false, i ? head_tag : torso_tag);
    add_fixture(b, i ? (b2Shape *)&circle : (b2Shape *)&box, friction, 0.01f, T_DRIVER, DRIVER_MASK, 0);
    b2RevoluteJointDef rj;
    rj.bodyA = prev;
    rj.bodyB = b;
    rj.localAnchorA = i ? b2Vec2(0, -0.3f) : prev->GetLocalPoint(b2Vec2(x, y));
    rj.localAnchorB.Set(-0.0f, 0.3f);
    rj.referenceAngle = i ? 0 : 3.14159265f;
    rj.enableLimit = true;
    rj.lowerAngle = -limit;
    rj.upperAngle = limit;
    b2RevoluteJoint *rev = (b2RevoluteJoint *)W->CreateJoint(&rj);
    if (i) r.neck = rev;
    /* a zero-length spring beside the joint keeps him upright */
    b2DistanceJointDef dj;
    dj.bodyA = prev;
    dj.bodyB = b;
    b2Vec2 side = b->GetWorldPoint(b2Vec2(-0.15f, 0.3f));
    dj.localAnchorA = i ? b2Vec2(-0.15f, -0.3f) : prev->GetLocalPoint(side);
    dj.localAnchorB.Set(-0.15f, 0.3f);
    dj.length = (prev->GetWorldPoint(dj.localAnchorA) - b->GetWorldPoint(dj.localAnchorB)).Length();
    float k = 1 + 0.25f * (float)i;
    dj.frequencyHz = 10.1f / k;
    dj.dampingRatio = 10.0f / k;
    r.spring[i] = W->CreateJoint(&dj);
    if (i) r.head = b;
    else r.torso = b;
    prev = b;
  }
  return r;
}

void create_driver(float x, float y, float limit, float friction) {
  Rider r = create_rider(x, y, limit, friction, &tag_torso, &tag_head);
  V.torso = r.torso, V.head = r.head, V.neck = r.neck;
  V.spring[0] = (b2DistanceJoint *)r.spring[0], V.spring[1] = (b2DistanceJoint *)r.spring[1];
}

b2WheelJoint *wheel_joint(b2Body *wheel, b2Vec2 axis, float hz, float damp) {
  b2WheelJointDef jd;
  jd.Initialize(V.chassis, wheel, wheel->GetPosition(), axis);
  jd.enableMotor = true;
  jd.maxMotorTorque = 500;
  jd.motorSpeed = 0;
  jd.frequencyHz = hz;
  jd.dampingRatio = damp;
  return (b2WheelJoint *)W->CreateJoint(&jd);
}

/* a wheel's motor (b2WheelJoint or, for the soft tire's hub, b2RevoluteJoint) */
void motor(int i, float torque, float speed) {
  if (V.rev_joint[i]) {
    ((b2RevoluteJoint *)V.joint[i])->SetMaxMotorTorque(torque);
    ((b2RevoluteJoint *)V.joint[i])->SetMotorSpeed(speed);
  } else {
    ((b2WheelJoint *)V.joint[i])->SetMaxMotorTorque(torque);
    ((b2WheelJoint *)V.joint[i])->SetMotorSpeed(speed);
  }
}
void motor_torque(int i, float torque) {
  if (V.rev_joint[i]) ((b2RevoluteJoint *)V.joint[i])->SetMaxMotorTorque(torque);
  else ((b2WheelJoint *)V.joint[i])->SetMaxMotorTorque(torque);
}

/* a circle wheel: its body, fixture and the tag that says it is wheel i */
b2Body *make_wheel(int i, float x, float y, float r, float friction, float density, int16 group) {
  b2CircleShape c;
  c.m_radius = V.wr[i] = r;
  tag_car[i + 1] = {T_CAR, i + 1};
  b2Body *b = make_body(x, y, 0, true, &tag_car[i + 1]);
  add_fixture(b, &c, friction, density, T_CAR, CAR_MASK, group);
  return V.wheel[i] = b;
}

/* the chassis: its polygon, at the spawn point + (0, 1); its centre of mass moved by (dx, dy) */
void make_chassis(int nv, const float (*poly)[2], float density, int16 group, float x, float y, float dx, float dy) {
  b2PolygonShape p;
  b2Vec2 vs[8];
  for (int i = 0; i < nv; i++) vs[i].Set(poly[i][0], poly[i][1]);
  p.Set(vs, nv);
  tag_car[0] = {T_CAR, 0};
  V.chassis = make_body(x, y + 1, 0, true, &tag_car[0]);
  add_fixture(V.chassis, &p, 0.2f, density, T_CAR, CAR_MASK, group);
  b2MassData md;
  V.chassis->GetMassData(&md);
  md.center.x += dx;
  md.center.y += dy;
  V.chassis->SetMassData(&md);
}

/* SoftTire::SoftTire: n balls of r 0.135 R around a hub of r ratio R, each on a spring (hz, damping 0.5) and a radial
 * slide that lets it in by ratio R; the hub turns on a revolute motor at the chassis */
void make_soft_tire(float cx, float cy, float R, float ratio, float hz, float density, float friction, int n) {
  float rb = R * 0.135f;
  V.nseg = n;
  for (int i = 0; i < n; i++) {
    float a = (float)i * (6.283185307179586f / (float)n);
    b2CircleShape c;
    c.m_radius = rb;
    V.seg[i] = make_body(cx + (R - rb) * cosf(a), cy + (R - rb) * sinf(a), 0, true, &tag_seg);
    add_fixture(V.seg[i], &c, friction, density, T_CAR, CAR_MASK, 0);
  }
  b2CircleShape hub;
  hub.m_radius = R * ratio;
  tag_car[1] = {T_CAR, 1};
  V.wheel[0] = make_body(cx, cy, 0, true, &tag_car[1]);
  V.wr[0] = R;
  add_fixture(V.wheel[0], &hub, friction, density, T_CAR, CAR_MASK, 0);
  for (int i = 0; i < n; i++) {
    float a = (float)i * (6.283185307179586f / (float)n);
    b2Vec2 at = V.seg[i]->GetWorldCenter();
    b2DistanceJointDef dj;
    dj.Initialize(V.seg[i], V.wheel[0], at, b2Vec2(cx, cy));
    dj.frequencyHz = hz;
    dj.dampingRatio = 0.5f;
    W->CreateJoint(&dj);
    b2PrismaticJointDef pj;
    pj.Initialize(V.seg[i], V.wheel[0], at, b2Vec2(-cosf(a), -sinf(a)));
    pj.enableLimit = true;
    pj.lowerTranslation = -(R * ratio);
    pj.upperTranslation = 0;
    W->CreateJoint(&pj);
  }
  b2RevoluteJointDef rj;
  rj.Initialize(V.chassis, V.wheel[0], V.wheel[0]->GetPosition());
  rj.enableMotor = true;
  rj.maxMotorTorque = 500;
  rj.motorSpeed = 0;
  V.joint[0] = W->CreateJoint(&rj);
  V.rev_joint[0] = true;
}

/* PlayerCarTractor::createExhaust: a pipe on a joint held straight; a knock bends it, a hard one breaks it off */
void make_exhaust(void) {
  b2Vec2 at = V.chassis->GetWorldPoint(b2Vec2(1.281f, 1.9215f));
  V.exhaust = make_body(at.x, at.y, 0, true, &tag_exhaust);
  b2PolygonShape box;
  box.SetAsBox(0.0915f, 0.68625f, b2Vec2(0, 0), 0);
  add_fixture(V.exhaust, &box, 0.2f, 0.65f, T_CAR, CAR_MASK, -1);
  b2Vec2 c = V.exhaust->GetWorldCenter();
  b2RevoluteJointDef rj;
  rj.Initialize(V.chassis, V.exhaust, b2Vec2(c.x, c.y - 1));
  rj.enableLimit = true;
  rj.lowerAngle = 0;
  rj.upperAngle = 0;
  V.exhaust_joint = (b2RevoluteJoint *)W->CreateJoint(&rj);
}

/* the five new vehicles' bodies and numbers (their classes' create and constructor) */
void create_new(int veh, const uint8_t lv[4], float x, float y) {
  const NewSpec *n = V.n = &NSPECS[veh - V_TRACTOR];
  int le = level_of(veh, lv, UP_ENGINE), ls = level_of(veh, lv, UP_SUSPENSION), lt = level_of(veh, lv, UP_TIRES);
  if (lt < 0) lt = level_of(veh, lv, UP_TRACKS);
  if (le < 1) le = 1;
  if (lt < 1) lt = 1;
  if (ls < 1) ls = 1;
  V.level_com = lv[n->com_up] < 1 ? 1 : lv[n->com_up];
  V.hz = n->hz;
  V.damp = n->damp0 * powi(n->damp_k, ls);
  V.speed = n->speed0 * powi(n->speed_k, le) * n->speed_m;
  V.turbo_cap = n->cap0 * powi(n->cap_k, le);
  V.rate = n->rate0 * powi(1.1f, le - n->rate_l);
  V.tires = n->tire0 * powi(n->tire_k, lt);
  V.awd = n->awd;
  V.air = n->air;
  float com = -(float)(V.level_com - 1) * n->com_k;
  switch (veh) {
    case V_TRACTOR:
      make_chassis(n->nv, n->poly, n->cdens, 0, x, y, 0, com);
      make_exhaust();
      V.nw = 2, V.ndriven = 2;
      make_soft_tire(x - 1.83f + 0.6039f, y + 0.251625f + 0.324825f, 1.09f, 0.6f, 6, 0.5f, V.tires, CAR_SEGS);
      make_wheel(1, x + 1.83f, y + 0.251625f + 0.25f, 0.47f, V.tires, 0.5f, 0);
      V.joint[1] = wheel_joint(V.wheel[1], b2Vec2(0, 1), V.hz, V.damp);
      V.share[0] = 1 - V.awd, V.share[1] = V.awd;
      V.gas[0] = n->gas_r, V.gas[1] = n->gas_f;
      V.wspeed[0] = V.speed, V.wspeed[1] = V.speed * 2.319149f;   /* (the front's radius is 0.47 / 1.09 the back's) */
      V.mud_a = 1, V.mud_b = 0;
      break;
    case V_BUS:
    case V_DIESEL:
    case V_TRUCK: {
      make_chassis(n->nv, n->poly, n->cdens, 0, x, y, 0, com);
      /* wheels: x from the spawn point, rear ones first; the last one is the front */
      static const float XS[3][3] = {{-2.15f, 2.95f, 0}, {-1.75f, 1.75f, 0}, {-1.4f, -2.65f, 2.8f}};
      int row = veh == V_BUS ? 0 : veh == V_DIESEL ? 1 : 2;
      float r = veh == V_DIESEL ? 0.65f : 0.55f;
      V.nw = V.ndriven = veh == V_TRUCK ? 3 : 2;
      for (int i = 0; i < V.nw; i++) {
        bool front = i == V.nw - 1;
        make_wheel(i, x + XS[row][i], y - 0.25f, r, V.tires, 0.2f, 0);
        V.joint[i] = wheel_joint(V.wheel[i], b2Vec2(0, 1), V.hz, V.damp);
        V.share[i] = front ? V.awd : 1 - V.awd;
        V.gas[i] = front ? n->gas_f : n->gas_r;
        V.wspeed[i] = V.speed;
      }
      V.mud_a = V.nw - 1, V.mud_b = 0;
      break;
    }
    case V_SNOWMOBILE: {
      /* the tank's sister: its centre of mass 1 m back, a ski in front (two wheels whose motors hold them still) and
       * a track of five wheels */
      bool snow = stage_id == ST_ARCTIC || stage_id == ST_ARCTIC_CAVE;
      if (snow) V.tires *= 3;
      if (stage_id == ST_ALIEN) V.tires *= 1.6f;
      V.damp = 1 - powf(0.9f, (float)ls);
      make_chassis(n->nv, n->poly, n->cdens, 1, x, y, -1, -0.07071428f * (float)(ls - 1));
      V.nw = 7, V.ndriven = 5;
      for (int i = 0; i < 2; i++) {   /* (the ski first, as the game makes it; kept after the track's five) */
        b2Vec2 at = V.chassis->GetWorldPoint(b2Vec2(1.5f + 1.1f * (float)i, -1.1f));
        make_wheel(5 + i, at.x, at.y, 0.28f * (1 + 0.2f * (float)i), snow ? 1.0f : 3.0f, 0.12f, -1);
        V.joint[5 + i] = wheel_joint(V.wheel[5 + i], b2Vec2(-0.1961161f, 0.9805807f), 6, 1);
      }
      for (int i = 0; i < 5; i++) {
        const float *t = SNOW_TRACK[i];
        make_wheel(i, x + t[0], y + t[1] + t[2] * V.damp, 0.28f * 1.2f,
                   V.tires - (float)(i + 1) / 5.0f * (V.tires * 0.25f), 0.3f, -1);
        float hz = t[3] > 0 ? t[3] : -t[3] * V.hz;
        V.joint[i] = wheel_joint(V.wheel[i], b2Vec2(0, 1), hz, 1);
        V.share[i] = 1 - V.awd;
        V.wspeed[i] = V.speed;
      }
      V.mud_a = 2, V.mud_b = 1;
      break;
    }
  }
  bool bike = veh == V_SNOWMOBILE;
  create_driver(x + n->seat[0], y + n->seat[1], bike ? 0.4f : 0.3f, bike ? 0.75f : 0.5f);
  if (veh == V_BUS) {
    V.npass = CAR_PASS;
    for (int p = 0; p < CAR_PASS; p++) {
      tag_pass[2 * p] = {T_CAR, -10 - 2 * p}, tag_pass[2 * p + 1] = {T_CAR, -11 - 2 * p};
      V.pass[p] = create_rider(x - 2.4f + 1.35f * (float)p, y + 2.35f, 0.3f, 0.5f, &tag_pass[2 * p], &tag_pass[2 * p + 1]);
    }
  }
}

/* PlayerCar*::PlayerCar and ::create: at x (the ground's height there + 3) */
void create_vehicle(int veh, const uint8_t lv[4], float x, float y) {
  memset(&V, 0, sizeof V);
  V.veh = veh;
  if (veh >= V_TRACTOR) {
    create_new(veh, lv, x, y);
    V.alive_at_turn = true;
    return;
  }
  const Spec *s = &SPECS[veh];
  V.s = s;
  int le = level_of(veh, lv, UP_ENGINE), ls = level_of(veh, lv, UP_SUSPENSION), lt = level_of(veh, lv, UP_TIRES);
  if (lt < 0) lt = level_of(veh, lv, UP_TRACKS);
  int la = level_of(veh, lv, UP_AWD), lair = level_of(veh, lv, UP_AIR), ld = level_of(veh, lv, UP_DOWNFORCE);
  if (le < 1) le = 1;
  if (ls < 1) ls = 1;
  if (lt < 1) lt = 1;
  V.hz = s->hz;
  V.damp = s->damp0 * powi(s->damp_k, s->damp_cap && ls > s->damp_cap ? s->damp_cap : ls);
  V.speed = s->speed0 * powi(s->speed_k, le) * s->speed_m;
  V.turbo_cap = s->turbo0 * powi(s->turbo_k, le);
  V.rate = s->rate0 * powi(1.1f, le - 1);
  V.tires = s->tire0 * powi(s->tire_k, lt);
  V.awd = la > 0 ? (float)(la - 1) / 18.0f : s->awd;
  V.air = s->air;
  if (veh == V_BIKE) V.air = 0.8f * powi(1.15f, (lair > 0 ? lair : 1) - 1);
  if (veh == V_QUAD) V.air = 0.8f * powi(1.15f, 4);   /* (the Quad's own, fixed) */
  V.down = s->down0 + s->down_k * (float)(ld > 0 ? ld : 0);
  /* the chassis */
  b2PolygonShape poly;
  b2Vec2 vs[8];
  for (int i = 0; i < s->nv; i++) vs[i].Set(s->poly[i][0], s->poly[i][1]);
  poly.Set(vs, s->nv);
  tag_car[0] = {T_CAR, 0};
  V.chassis = make_body(x, y + 1, 0, true, &tag_car[0]);
  add_fixture(V.chassis, &poly, s->cfric, s->cdens, T_CAR, CAR_MASK, veh == V_TANK ? 1 : 0);
  /* the suspension lowers the centre of mass */
  b2MassData md;
  V.chassis->GetMassData(&md);
  md.center.y -= (float)(ls - 1) * s->com_k;
  V.chassis->SetMassData(&md);
  /* the wheels */
  if (veh == V_TANK) {
    V.nw = V.ndriven = 6;
    for (int i = 0; i < 6; i++) {
      const float *t = TANK_WHEELS[i];
      make_wheel(i, x + t[0], y + t[1], s->r, V.tires - (float)(i + 1) / 6.0f * (V.tires * 0.25f), s->wdens, -1);
      float hz = t[4] > 0 ? t[4] : -t[4] * V.hz, damp = t[4] > 0 ? t[5] : V.damp;
      V.joint[i] = wheel_joint(V.wheel[i], b2Vec2(t[2], t[3]), hz, damp);
    }
    V.mud_a = 4, V.mud_b = 1;
  } else {
    V.nw = V.ndriven = 2;
    for (int i = 0; i < 2; i++) {
      make_wheel(i, x + (i ? s->hwb : -s->hwb), y - s->wy, i ? s->r : s->rr, i ? V.tires * s->front_fric : V.tires,
                 s->wdens, 0);
      b2Vec2 axis = i && s->fork ? b2Vec2(-0.410365f, 0.911922f) : b2Vec2(0, 1);
      V.joint[i] = wheel_joint(V.wheel[i], axis, V.hz, V.damp);
    }
    V.share[0] = 1 - V.awd, V.share[1] = V.awd;
    V.mud_a = 1, V.mud_b = 0;
  }
  create_driver(x + s->seat[0], y + s->seat[1], s->neck_limit, s->driver_fric);
  V.alive_at_turn = true;
}

/* PlayerCar*::reportTouch: what touched which part */
void report_touch(int type, const Tag *part) {
  if (part->part == 0) V.touch_chassis |= (unsigned)type;
  else if (part->part > 0) V.touch_wheel[part->part - 1] |= (unsigned)type;
  else if (part->part == -2 && !V.dead && !(type & NONLETHAL)) V.head_hit = true;
  else if (part->part == -4) V.touch_exhaust |= (unsigned)type;
  else if (part->part == -5) V.touch_wheel[0] |= (unsigned)type;   /* (SoftTire::isSubBody: the rear wheel) */
  else if (part->part <= -10) V.pass_hit[(-10 - part->part) / 2] = true;   /* (a passenger touched something) */
}

struct Listener : b2ContactListener {
  void PreSolve(b2Contact *contact, const b2Manifold *old) override {
    (void)old;
    Tag *a = (Tag *)contact->GetFixtureA()->GetBody()->GetUserData();
    Tag *b = (Tag *)contact->GetFixtureB()->GetBody()->GetUserData();
    if (!a || !b) return;
    if (a->type == T_CAR && b->type != T_CAR) report_touch(b->type, a);
    else if (b->type == T_CAR && a->type != T_CAR) report_touch(a->type, b);
  }
} listener;

/* ------------------------------------------------------------------ the ground: 0.5 m edges, as the game's Terrain */
const float SEG = 0.5f, X0 = -40.0f;
const int RING = 64;   /* the edges kept around the car (the game keeps 200; only these near it can be touched) */
struct Ring {
  b2Fixture *f[RING];
  int k0, k1;          /* edges k0..k1-1 exist; edge k from vertex k to k + 1 */
  bool ceiling;
};
Ring floor_ring, ceil_ring;
int game_end;          /* the game's last vertex: 40 m ahead of the farthest the car got */
b2Fixture *wall;
float wall_x;

float vertex_y(int k, bool ceiling) {
  /* the game's Terrain::addSegment: vertex k at X0 + 0.5 k has the height of the x before it */
  return ceiling ? stage_roof_vertex(k) : stage_vertex(k);
}

b2Fixture *edge(float xa, float ya, float xb, float yb) {
  b2EdgeShape e;
  e.Set(b2Vec2(xa, ya), b2Vec2(xb, yb));
  b2FixtureDef fd;
  fd.shape = &e;
  fd.friction = stage->friction;
  fd.restitution = 0;
  fd.density = 0;
  fd.filter.categoryBits = T_GROUND;
  fd.filter.maskBits = GROUND_MASK;
  return ground->CreateFixture(&fd);
}

void ring_set(Ring *r, int lo, int hi) {
  if (hi - lo > RING) lo = hi - RING;
  /* drop what left the window */
  while (r->k0 < r->k1 && r->k0 < lo) ground->DestroyFixture(r->f[r->k0++ % RING]);
  while (r->k1 > r->k0 && r->k1 > hi) ground->DestroyFixture(r->f[--r->k1 % RING]);
  if (r->k0 >= r->k1) r->k0 = r->k1 = lo;
  /* and add what entered it, on both sides */
  while (r->k0 > lo) {
    int k = --r->k0;
    r->f[k % RING] = edge(X0 + SEG * (float)k, vertex_y(k, r->ceiling), X0 + SEG * (float)(k + 1),
                          vertex_y(k + 1, r->ceiling));
  }
  while (r->k1 < hi) {
    int k = r->k1++;
    r->f[k % RING] = edge(X0 + SEG * (float)k, vertex_y(k, r->ceiling), X0 + SEG * (float)(k + 1),
                          vertex_y(k + 1, r->ceiling));
  }
}

/* the ground's edges around the car; the game's wall behind (Terrain::updateBackWall: 20 edges into its ring of 200) */
void update_ground(float cx) {
  int want = (int)ceilf((cx + 40 - X0) / SEG);
  if (want > game_end) game_end = want;
  int start = game_end - 200 < 0 ? 0 : game_end - 200;
  int wk = start + 20;
  float wx = X0 + SEG * (float)wk;
  if (game_end - start >= 21 && wx != wall_x) {
    if (wall) ground->DestroyFixture(wall);
    float wy = vertex_y(wk, false);
    wall = edge(wx, wy - 100, wx, wy + 1000);
    wall_x = wx;
  }
  /* (wider in the Forest: its fallen trees roll on ahead of the car) */
  float back = stage->feature == FEAT_TREES ? 12 : 8, ahead = stage->feature == FEAT_TREES ? 18 : 10;
  int lo = (int)floorf((cx - back - X0) / SEG), hi = (int)ceilf((cx + ahead - X0) / SEG);
  if (lo < start) lo = start;
  if (hi > game_end) hi = game_end;
  ring_set(&floor_ring, lo, hi);
  if (stage->ceiling) ring_set(&ceil_ring, lo, hi);
}

/* ------------------------------------------------------------------ rope bridges (Bridge::create) */
const int MAXPLANK = 20;
struct Bridge {
  int k;               /* which bridge, or -1 */
  int n;
  b2Body *plank[MAXPLANK];
} bridges[2];

void bridge_free(Bridge *br) {
  for (int i = 0; i < br->n; i++) W->DestroyBody(br->plank[i]);   /* (its joints go with it) */
  br->n = 0, br->k = -1;
}

void bridge_make(Bridge *br, int k) {
  float x0, len, y0;
  if (!stage_bridge_k(k, &x0, &len, &y0)) return;
  int n = (int)len;
  if (n > MAXPLANK) n = MAXPLANK;
  float x1 = x0 + len, t = 0.3f;
  float w = (x1 - x0) / (float)(n - 1), dx = (x1 - x0) / (float)n;
  b2PolygonShape box;
  box.SetAsBox(w * 0.5f, t * 0.5f);
  b2Body *prev = ground;
  float cx = x0 + w, cy = y0 - t * 0.5f;
  br->k = k, br->n = n;
  for (int i = 0; i < n; i++) {
    b2BodyDef bd;
    bd.type = b2_dynamicBody;
    bd.position.Set(cx, cy);
    bd.userData = &tag_bridge;
    b2Body *p = W->CreateBody(&bd);
    b2FixtureDef fd;
    fd.shape = &box;
    fd.friction = 0.5f;
    fd.density = 1;
    fd.filter.categoryBits = T_BRIDGE;
    fd.filter.maskBits = BRIDGE_MASK;
    p->CreateFixture(&fd);
    b2RevoluteJointDef rj;
    rj.Initialize(prev, p, b2Vec2(cx - w * 0.5f, cy + t * 0.5f));
    W->CreateJoint(&rj);
    br->plank[i] = prev = p;
    cx += dx;
  }
  b2RevoluteJointDef rj;
  rj.Initialize(prev, ground, b2Vec2(cx - w * 0.5f, cy + t * 0.5f));
  W->CreateJoint(&rj);
}

/* the bridges near the car exist, the others not */
void update_bridges(float cx) {
  if (!stage->bridges) return;
  int near = -1;
  for (int k = stage_bridge_first(cx - 40);; k++) {
    float x0, len, y0;
    if (!stage_bridge_k(k, &x0, &len, &y0) || x0 > cx + 45) break;
    if (x0 + len < cx - 30) continue;
    near = k;
    break;
  }
  for (int i = 0; i < 2; i++)
    if (bridges[i].k >= 0 && bridges[i].k != near) bridge_free(&bridges[i]);
  if (near < 0) return;
  for (int i = 0; i < 2; i++)
    if (bridges[i].k == near) return;
  for (int i = 0; i < 2; i++)
    if (bridges[i].k < 0) {
      bridge_make(&bridges[i], near);
      return;
    }
}

/* ------------------------------------------------------------------ pits (Pit::create) */
/* a static sensor box len x depth under the pit's surface; the car's parts touching it are in mud or lava */
struct PitBody {
  int k;               /* which pit, or -1 */
  b2Body *b;
} pits[2];

void pit_make(PitBody *p, int k) {
  float x0, len, y;
  if (!stage_pit_k(k, &x0, &len, &y)) return;
  float depth = stage_pit_depth();
  b2BodyDef bd;
  bd.position.Set(x0 + len * 0.5f + 0.5f, y - (depth + 0.5f) * 0.5f);
  bd.userData = stage->pits == PIT_LAVA ? &tag_lava : &tag_mud;
  p->b = W->CreateBody(&bd);
  b2PolygonShape box;
  box.SetAsBox(len * 0.5f, depth * 0.5f);
  b2FixtureDef fd;
  fd.shape = &box;
  fd.friction = 0.2f;
  fd.isSensor = true;
  fd.filter.categoryBits = stage->pits == PIT_LAVA ? T_LAVA : T_MUD;
  fd.filter.maskBits = PIT_MASK;
  p->b->CreateFixture(&fd);
  p->k = k;
}

void update_pits(float cx) {
  if (!stage->pits) return;
  int near = -1;
  for (int k = stage_pit_first(cx - 40);; k++) {
    float x0, len, y;
    if (!stage_pit_k(k, &x0, &len, &y) || x0 > cx + 45) break;
    if (x0 + len + 1 < cx - 30) continue;
    near = k;
    break;
  }
  for (int i = 0; i < 2; i++)
    if (pits[i].k >= 0 && pits[i].k != near) W->DestroyBody(pits[i].b), pits[i].k = -1, pits[i].b = NULL;
  if (near < 0) return;
  for (int i = 0; i < 2; i++)
    if (pits[i].k == near) return;
  for (int i = 0; i < 2; i++)
    if (pits[i].k < 0) {
      pit_make(&pits[i], near);
      return;
    }
}

/* ------------------------------------------------------------------ trees (the Forest's: Tree::create) */
const int MAXTREE = 10;
struct TreeBody {
  int k;               /* the tree's index, or -1 */
  b2Body *b;
  b2RevoluteJoint *j;
  float t;             /* seconds since it broke */
  float held;          /* seconds pushed against its stop */
} trees[MAXTREE];
int tree_broken;

void tree_free(TreeBody *t) {
  if (t->b) W->DestroyBody(t->b);
  t->b = NULL, t->j = NULL, t->k = -1;
}

/* a tree is four parts 0.45 x 0.85 m (density 0.2 x the vehicle's obstacle weight, friction 0.8), each held to the
 * one under it by a joint that bends +-0.05 rad and a spring; here the four are one trunk on one such joint */
void tree_make(TreeBody *t, int k, float weight) {
  float x, y;
  if (!stage_tree(k, &x, &y)) return;
  b2PolygonShape box;
  box.SetAsBox(0.225f, 0.425f * 4, b2Vec2(0, 0.425f * 4), 0);
  b2BodyDef bd;
  bd.type = b2_dynamicBody;
  bd.position.Set(x, y);
  bd.userData = &tag_tree;
  t->b = W->CreateBody(&bd);
  b2FixtureDef fd;
  fd.shape = &box;
  fd.friction = 0.8f;
  fd.density = 0.2f * weight;
  fd.filter.categoryBits = T_TREE;
  fd.filter.maskBits = TREE_MASK;
  t->b->CreateFixture(&fd);
  b2RevoluteJointDef rj;
  rj.Initialize(ground, t->b, b2Vec2(x, y));
  rj.enableLimit = true;
  rj.lowerAngle = -0.05f;
  rj.upperAngle = 0.05f;
  t->j = (b2RevoluteJoint *)W->CreateJoint(&rj);
  t->k = k, t->t = 0, t->held = 0;
}

/* PlayerCar*::getObstacleWeightMultiplier */
const float OBSTACLE_WEIGHT[V_TANK + 1] = {0.85f, 1.2f, 0.45f, 1.0f, 1.0f, 1.0f, 0.95f, 0.6f};
float obstacle_weight(int veh) { return veh >= V_TRACTOR ? NSPECS[veh - V_TRACTOR].weight : OBSTACLE_WEIGHT[veh]; }

void update_trees(float cx) {
  tree_broken = -1;
  if (stage->feature != FEAT_TREES) return;
  for (int i = 0; i < MAXTREE; i++) {
    TreeBody *t = &trees[i];
    if (t->k < 0) continue;
    float x = t->b->GetPosition().x;
    /* Tree::onSimulate: a part bent past 0.3 rad breaks off. The game's four light parts give way to a knock or
     * a push; the one trunk here breaks on a knock or when held at its stop for 0.75 s */
    if (t->j) {
      bool stop = fabsf(t->j->GetJointAngle()) > 0.045f;
      t->held = stop ? t->held + DT : 0;
    }
    if (t->j && (fabsf(t->j->GetJointAngle()) > 0.3f || fabsf(t->j->GetReactionTorque(CAR_HZ)) > 40 || t->held > 0.75f)) {
      W->DestroyJoint(t->j);
      t->j = NULL;
      stage_tree_fall(t->k);
      tree_broken = t->k;
    }
    if (!t->j) {
      t->t += DT;
      if (t->t > 1) {   /* fallen: passes through after a second, gone after four */
        b2Filter f;
        f.categoryBits = 0, f.maskBits = 0;
        t->b->GetFixtureList()->SetFilterData(f);
      }
      if (t->t > 4) tree_free(t);
      continue;
    }
    if (x < cx - 12 || x > cx + 24) tree_free(t);
  }
  for (int k = stage_tree_first(cx - 10);; k++) {
    float x, y;
    if (!stage_tree(k, &x, &y) || x > cx + 20) break;
    if (x < cx - 10 || !stage_tree_standing(k)) continue;
    bool have = false;
    for (int i = 0; i < MAXTREE; i++) have = have || trees[i].k == k;
    if (have) continue;
    for (int i = 0; i < MAXTREE; i++)
      if (trees[i].k < 0) {
        tree_make(&trees[i], k, obstacle_weight(V.veh));
        break;
      }
  }
}

/* ------------------------------------------------------------------ driving (accelerate, decelerate, idleEngine) */
float wheel_w(int i) { return V.wheel[i]->GetAngularVelocity(); }
bool tracked(void) { return V.veh == V_TANK || V.veh == V_SNOWMOBILE; }

/* PlayerCar::baseAccelerate, baseDeccelerate: in a Mudpool's mud the car swims ahead (or back) */
void base_pedal(int pedal) {
  if (stage->pits != PIT_MUD) return;
  b2Body *ch = V.chassis;
  for (int pass = 0; pass < 2; pass++) {
    if (pass ? V.mud_frames_b <= 0 : V.mud_frames_a <= 0) continue;
    float t = 0.2f * clampf(1 - ch->GetAngularVelocity(), 0, 1);
    ch->ApplyForceToCenter(b2Vec2(5.0f * (float)pedal, 0));
    ch->ApplyTorque(pedal > 0 ? t : -t);
  }
}

/* the air control: cars push while 5 steps in the air; the bikes ease in over 15 more, always */
void air_control(int pedal) {
  b2Body *ch = V.chassis;
  float w = ch->GetAngularVelocity();
  bool bike = V.veh == V_BIKE || V.veh == V_QUAD;
  int reps = tracked() ? V.ndriven : 1;   /* (the Tank and the Snow Mobile push once a track wheel) */
  for (int r = 0; r < reps; r++) {
    if (bike) {
      float a = clampf((float)(V.air_frames - 5) / 15.0f, 0, 1) * V.air;
      if (pedal > 0) ch->ApplyTorque(20 * a * clampf(a - w, 0, 1));
      else ch->ApplyTorque(20 * a * clampf(-a - w, -1, 0));
    } else if (pedal > 0 && V.air_frames > 5) {
      ch->ApplyTorque(V.air * clampf(1 - w, 0, 1));
    } else if (pedal < 0 && V.air_frames > 0) {
      ch->ApplyTorque(V.air * clampf(-1 - w, -1, 0));
    }
  }
}

void idle_engine(void) {
  for (int i = 0; i < V.ndriven; i++) motor_torque(i, 0);
  V.accelerating = false;
}

void accelerate(void) {
  base_pedal(1);
  air_control(1);
  if (tracked()) {
    for (int i = 0; i < V.ndriven; i++) {
      float f = clampf(V.speed - fabsf(wheel_w(i)), 0, 1);
      float t = (float)(i + 1) / (float)V.ndriven * 2 + (V.turbo + 0.5f) * ((1 - V.awd) * (V.s ? V.s->torque : V.n->torque) * f);
      if (V.air_frames > 0) t *= 0.25f;
      motor(i, t, -V.speed);
    }
  } else if (V.n) {   /* the new vehicles: each wheel its share, rear and front their own turbo boost */
    for (int i = 0; i < V.ndriven; i++) {
      float f = clampf(V.wspeed[i] - fabsf(wheel_w(i)), 0, 1);
      float t = V.share[i] * V.n->torque * f * (V.gas[i] + V.n->gas_k * V.turbo);
      if (V.air_frames > 0) t *= 0.25f;
      motor(i, t, -V.wspeed[i]);
    }
  } else {
    for (int i = 0; i < 2; i++) {
      float f = clampf(V.speed - fabsf(wheel_w(i)), 0, 1);
      float t = V.share[i] * V.s->torque * f * (0.5f + V.turbo);
      if (V.air_frames > 0) t *= 0.25f;
      motor(i, t, -V.speed);
    }
  }
  V.accelerating = true;
}

void decelerate(void) {
  base_pedal(-1);
  V.decel_air = true;
  air_control(-1);
  bool tank = tracked();
  if (V.chassis->GetLinearVelocity().x > 2.5f) {   /* rolling forward: brake */
    float b = V.n ? V.n->brake : 20, ba = V.n ? V.n->brake_air : 1;
    for (int i = 0; i < V.ndriven; i++) {
      float t = V.air_frames > 5 ? ba : b;
      if (tank) t = (t + (float)(i + 1) / (float)V.ndriven * 2) * 0.5f;
      motor(i, t, 0);
    }
    V.accelerating = false;
    return;
  }
  float torque = V.s ? V.s->torque : V.n->torque;
  int mode = V.n ? V.n->reverse : 0;
  for (int i = 0; i < V.ndriven; i++) {   /* else reverse */
    float f = clampf(V.wspeed[i] > 0 ? V.wspeed[i] - fabsf(wheel_w(i)) : V.speed - fabsf(wheel_w(i)), 0, 1);
    bool front = V.n && i == V.ndriven - 1;
    float t;
    if (tank) t = ((float)(i + 1) / (float)V.ndriven * 2 + (0.5f + 0.5f * V.turbo) * ((1 - V.awd) * torque * powf(f, 4.05f))) * 0.5f;
    else if (mode == 1 && !front) t = V.share[i] * torque * f * (0.3f + 0.7f * V.turbo);
    else if (mode == 2) t = V.share[i] * torque * f * (1 + V.turbo) * 0.5f;
    else t = V.share[i] * torque * powf(f, 4.05f) * (0.5f + 0.5f * V.turbo);
    motor(i, t, V.wspeed[i] > 0 ? V.wspeed[i] : V.speed);
  }
  V.accelerating = true;
}

/* ------------------------------------------------------------------ after each step (PlayerCar*::update) */
void release_rider(Rider *r) {
  for (int i = 0; i < 2; i++)
    if (r->spring[i]) W->DestroyJoint(r->spring[i]), r->spring[i] = NULL;
  if (r->neck) r->neck->SetLimits(-1.22173f, 1.047198f);
}

void handle_head_hit(void) {
  if (!V.head_hit) return;
  V.head_hit = false;
  V.dead = true;
  for (int i = 0; i < 2; i++)
    if (V.spring[i]) W->DestroyJoint(V.spring[i]), V.spring[i] = NULL;
  V.neck->SetLimits(-1.22173f, 1.047198f);
}

/* PlayerCar::handleFlips: a turn counts once the chassis is back within 90 degrees of upright */
int handle_flips(void) {
  float deg = V.chassis->GetAngle() * 57.29578f;
  int id = (int)deg;
  int k = (deg > 0 ? id + 90 : id - 90) / 360 * 360;
  float d = deg - (float)k;
  if (!(d > -90 && d < 90)) return FLIP_NONE;
  int r = FLIP_NONE;
  if (k != V.flips_k) {
    if (V.alive_at_turn && V.dead) r = FLIP_NECK;
    else if (k < V.flips_k) r = FLIP_FRONT;
    else r = FLIP_BACK;
  }
  V.flips_k = k;
  V.alive_at_turn = !V.dead;
  return r;
}

void downforce(void) {
  if (V.down <= 0) return;
  b2Body *ch = V.chassis;
  b2Vec2 v = ch->GetLinearVelocity();
  float sp = v.Length();
  if (sp < 1.1920929e-7f) return;
  float a = ch->GetAngle();
  float f = clampf(sp - 3, 0, 150) * clampf((cosf(a) * v.x / sp + sinf(a) * v.y / sp - 0.75f) * 3, 0, 1) * V.down;
  ch->ApplyForceToCenter(b2Vec2(sinf(a) * f, -cosf(a) * f));
}

/* a force toward the chassis' floor (the Tractor's stability, the Snow Mobile's weight) */
void push_down(float f) {
  float a = V.chassis->GetAngle() + 3.14159f;
  V.chassis->ApplyForceToCenter(b2Vec2(-sinf(a) * f, cosf(a) * f));
}

/* PlayerCar::baseUpdate: mud (the Mudpool) and lava (the Volcano). a and b: the touches of the two parts the vehicle
 * hands it (its front and back wheels for most: the game calls them the chassis and the wheels) */
void base_update(unsigned a, unsigned b) {
  const unsigned PITS = T_MUD | T_LAVA;
  b2Body *ch = V.chassis;
  if (stage->pits == PIT_MUD) {
    float damp = V.n ? V.n->mud_damp : 4;
    if (a & PITS) {
      ch->SetLinearDamping(damp);
      ch->SetAngularDamping(damp);
      ch->ApplyForceToCenter(b2Vec2(0, 10));
      V.mud_frames_a++, V.mud_frames_b++;
    } else if (b & PITS) {
      ch->SetLinearDamping(damp * 0.5f);
      ch->ApplyForceToCenter(b2Vec2(0, 10 * 0.5f));
      V.mud_frames_b++;
    } else {
      ch->SetLinearDamping(0);
      ch->SetAngularDamping(0);
      V.mud_frames_a = V.mud_frames_b = 0;
    }
  }
  if (stage->pits == PIT_LAVA) {
    int *c = V.burn;
    bool on_a = a & T_LAVA, on_b = b & T_LAVA;
    if (!on_a && !on_b) {
      for (int i = 0; i < 5; i++)
        if (c[i] > 0) c[i]--;
    } else {
      /* the fire spreads from what touches the lava: c4 the "chassis", c0 the "wheels", to c3, c1, then c2 */
      int old4 = c[4];
      if (c[4] > 10 || c[0] > 10) c[3]++;
      if (c[3] > 10) c[1]++;
      if (c[1] > 10) c[2]++;
      if (on_a || c[2] > 10) c[4] = old4 + 1;
      if (on_b || c[2] > 10) c[0]++;
    }
  }
}

/* PlayerCarTractor::handleExhaustBreak: touched, the pipe bends to where it is; past 0.3 rad it breaks off */
void handle_exhaust(void) {
  if (!V.exhaust_joint || !V.touch_exhaust) return;
  float a = V.exhaust_joint->GetJointAngle();
  if (a < -0.3f || a > 0.3f) {
    W->DestroyJoint(V.exhaust_joint);
    V.exhaust_joint = NULL;
    return;
  }
  float lo = V.exhaust_joint->GetLowerLimit(), hi = V.exhaust_joint->GetUpperLimit();
  if (a < 0) V.exhaust_joint->SetLimits(a <= lo ? a : lo, hi);
  else V.exhaust_joint->SetLimits(lo, a >= hi ? a : hi);
}

/* PlayerCarTractor::update's stability: when only one end's wheel touches, a torque rights it; while one does, a
 * force holds it to the ground */
void tractor_stability(void) {
  bool rear = V.touch_wheel[0] != 0, front = V.touch_wheel[1] != 0;
  if (!rear && !front) return;
  float t = (float)(V.level_com - 1) / 14.0f * 25;
  if (front && !rear) V.chassis->ApplyTorque(t);
  if (rear && !front) V.chassis->ApplyTorque(-t);
  push_down((float)V.level_com / 14.0f * 6);
}

int update(void) {
  /* PlayerCar::baseUpdate first (it reads this step's touches) */
  base_update(V.mud_a >= 0 ? V.touch_wheel[V.mud_a] : 0, V.mud_b >= 0 ? V.touch_wheel[V.mud_b] : 0);
  /* the engine's revs (the meter): towards 1 + |rear wheel| / 5 with gas, 1 without */
  float wr = V.veh == V_SNOWMOBILE ? fabsf(wheel_w(1) + wheel_w(2)) * 0.5f / 3 : fabsf(wheel_w(0));
  if (V.veh == V_BUS) wr *= 1 - V.awd;
  if (V.accelerating) V.rev += DT * 6 * (1 + wr / 5 - V.rev);
  else V.rev += DT * 3 * (1 - V.rev);
  unsigned any = V.touch_chassis | V.touch_exhaust;
  for (int i = 0; i < V.nw; i++) any |= V.touch_wheel[i];
  if (V.veh == V_TRACTOR) tractor_stability();
  if (any) V.air_frames = 0, V.decel_air = false;
  else V.air_frames++;
  handle_exhaust();
  handle_head_hit();
  for (int p = 0; p < V.npass; p++)   /* (a bus passenger who touched the ground goes limp) */
    if (V.pass_hit[p]) release_rider(&V.pass[p]), V.pass_hit[p] = false;
  int flip = handle_flips();
  downforce();
  if (V.veh == V_SNOWMOBILE) push_down(1.5f);
  /* the turbo builds while the gas is down */
  float d0 = V.n ? V.n->decay0 : 1, d1 = V.n ? V.n->decay1 : 4;
  if (V.accelerating) V.turbo += (0.35f + 0.75f * V.turbo) * V.rate / 60.0f;
  else V.turbo -= DT * (d0 + d1 * V.turbo);
  V.turbo = clampf(V.turbo, 0, V.turbo_cap);
  return flip;
}

void snapshot(Car *c, int flip) {
  b2Body *b[3] = {V.chassis, V.torso, V.head};
  CarBody *o[3] = {&c->ch, &c->torso, &c->head};
  for (int i = 0; i < 3; i++) o[i]->x = b[i]->GetPosition().x, o[i]->y = b[i]->GetPosition().y, o[i]->a = b[i]->GetAngle();
  for (int i = 0; i < V.nw; i++) {
    c->wh[i].x = V.wheel[i]->GetPosition().x, c->wh[i].y = V.wheel[i]->GetPosition().y;
    c->wh[i].a = V.wheel[i]->GetAngle();
    c->ground[i] = V.touch_wheel[i] != 0;
  }
  for (int p = 0; p < V.npass; p++)
    for (int k = 0; k < 2; k++) {
      b2Body *pb = k ? V.pass[p].head : V.pass[p].torso;
      c->pass[p][k].x = pb->GetPosition().x, c->pass[p][k].y = pb->GetPosition().y, c->pass[p][k].a = pb->GetAngle();
    }
  for (int i = 0; i < V.nseg; i++) c->seg[i][0] = V.seg[i]->GetPosition().x, c->seg[i][1] = V.seg[i]->GetPosition().y;
  if (V.exhaust) {
    c->has_exhaust = true;
    c->exhaust.x = V.exhaust->GetPosition().x, c->exhaust.y = V.exhaust->GetPosition().y, c->exhaust.a = V.exhaust->GetAngle();
  }
  c->vx = V.chassis->GetLinearVelocity().x, c->vy = V.chassis->GetLinearVelocity().y;
  c->w = V.chassis->GetAngularVelocity();
  c->air = V.air_frames;
  c->dead = V.dead;
  c->flip = flip;
  c->rev = V.rev, c->turbo = V.turbo;
  c->tree_broken = tree_broken;
  c->burning = 0;
  for (int i = 0; i < 5; i++) {
    c->burn[i] = (uint8_t)(V.burn[i] > 255 ? 255 : V.burn[i]);
    c->burning += V.burn[i] > 0;
  }
}

}   // namespace

/* ------------------------------------------------------------------ the C side */
extern "C" void car_init(Car *c, int veh, const uint8_t levels[4]) {
  car_free();
  W = new (world_mem) b2World(b2Vec2(0, -stage->gravity));
  W->SetContactListener(&listener);
  b2BodyDef bd;
  bd.userData = &tag_ground;
  ground = W->CreateBody(&bd);
  memset(&floor_ring, 0, sizeof floor_ring);
  memset(&ceil_ring, 0, sizeof ceil_ring);
  ceil_ring.ceiling = true;
  game_end = 0, wall = NULL, wall_x = -1e9f;
  for (int i = 0; i < 2; i++) bridges[i].k = -1, bridges[i].n = 0, pits[i].k = -1, pits[i].b = NULL;
  for (int i = 0; i < MAXTREE; i++) trees[i].k = -1, trees[i].b = NULL, trees[i].j = NULL;
  /* GameContentLayer::init: the vehicle 3 m over the ground at 0 */
  float y = stage_floor(0) + 3;
  update_ground(0);
  create_vehicle(veh, levels, 0, y);
  memset(c, 0, sizeof *c);
  c->veh = veh, c->nw = V.nw;
  for (int i = 0; i < V.nw; i++) c->wr[i] = V.wr[i];
  snapshot(c, FLIP_NONE);
}

/* the bridges' planks and the trees as they were when the world went (run_finish): the end of a run still draws them */
static CarBody kept_planks[2 * MAXPLANK], kept_trees[MAXTREE];
static int kept_which[MAXTREE], nkept_planks, nkept_trees;

extern "C" void car_free(void) {
  if (W) {
    nkept_planks = car_planks(kept_planks, 2 * MAXPLANK);
    nkept_trees = car_trees(kept_trees, kept_which, MAXTREE);
    W->~b2World();
  }
  W = NULL;
}

extern "C" void car_step(Car *c, int pedal, bool engine) {
  if (!W) return;
  /* GameContentLayer::preFrameUpdate: the controls */
  if (V.dead || !engine || pedal == 0) idle_engine();
  else if (pedal < 0) decelerate();
  else accelerate();
  /* PhysicsSimulation::simulate */
  float cx = V.chassis->GetPosition().x;
  update_ground(cx);
  update_bridges(cx);
  update_pits(cx);
  update_trees(cx);
  V.touch_chassis = V.touch_exhaust = 0;
  for (int i = 0; i < V.nw; i++) V.touch_wheel[i] = 0;
  W->Step(DT, 8, 3);
  snapshot(c, update());
}

extern "C" void car_point(const Car *c, float lx, float ly, float *wx, float *wy) {
  float s = sinf(c->ch.a), co = cosf(c->ch.a);
  *wx = c->ch.x + co * lx - s * ly;
  *wy = c->ch.y + s * lx + co * ly;
}

extern "C" float car_speed(const Car *c) { return sqrtf(c->vx * c->vx + c->vy * c->vy); }

extern "C" bool car_airborne(const Car *c) { return c->air > 0; }

extern "C" bool car_touches_box(const Car *c, float x, float y, float half) {
  (void)c;
  if (!W) return false;
  b2PolygonShape box;
  box.SetAsBox(half, half);
  b2Transform bx;
  bx.Set(b2Vec2(x, y), 0);
  b2Body *parts[3 + CAR_MAXW + 2 * CAR_PASS + CAR_SEGS + 1] = {V.chassis, V.torso, V.head};
  int n = 3;
  for (int i = 0; i < V.nw; i++) parts[n++] = V.wheel[i];
  for (int p = 0; p < V.npass; p++) parts[n++] = V.pass[p].torso, parts[n++] = V.pass[p].head;
  for (int i = 0; i < V.nseg; i++) parts[n++] = V.seg[i];
  if (V.exhaust) parts[n++] = V.exhaust;
  for (int i = 0; i < n; i++) {
    b2Vec2 p = parts[i]->GetPosition();
    if (fabsf(p.x - x) > half + 3 || fabsf(p.y - y) > half + 3) continue;
    for (b2Fixture *f = parts[i]->GetFixtureList(); f; f = f->GetNext())
      if (b2TestOverlap(&box, 0, f->GetShape(), 0, bx, parts[i]->GetTransform())) return true;
  }
  return false;
}

extern "C" float car_fuel_rate(int veh, const uint8_t levels[4]) {
  int lf = level_of(veh, levels, UP_FUEL);
  switch (veh) {
    case V_BUGGY: return 1.65f - 0.9f * (float)lf / 21.0f;
    case V_TANK: return 1.5f - 0.9f * (float)lf / 21.0f;
    case V_TRACTOR: return 1.2f - 0.7f * (float)(lf - 1) / 19.0f;
    case V_BUS: case V_TRUCK: return 1.5f - 1.1f * (float)lf / 26.0f;
    case V_DIESEL: case V_SNOWMOBILE: return 1.5f - 0.9f * (float)lf / 21.0f;
    default: return 1;
  }
}

extern "C" void car_meters(int veh, float *rev_k, float *rev_b, float *turbo_k) {
  *rev_b = 0;
  switch (veh) {
    case V_MONSTER: *rev_k = 1.65f, *turbo_k = 0.87f; break;
    case V_BUGGY: *rev_k = 1.09f, *turbo_k = 0.66f; break;
    case V_TANK: *rev_k = 1, *turbo_k = 0.44f; break;
    case V_TRACTOR: *rev_k = 1.6f, *turbo_k = 0.67f; break;
    case V_BUS: case V_DIESEL: case V_TRUCK: *rev_k = 1.6f, *turbo_k = 0.755f; break;
    case V_SNOWMOBILE: *rev_k = 4.125f * 0.55f, *rev_b = -1, *turbo_k = 0.5f * 0.43f; break;
    default: *rev_k = 1, *turbo_k = 1; break;
  }
}

#ifdef HOST
/* (tests: the numbers a vehicle was built with) */
extern "C" void car_numbers(float out[12]) {
  out[0] = V.speed, out[1] = V.s ? V.s->torque : V.n->torque, out[2] = V.tires, out[3] = V.hz, out[4] = V.damp;
  out[5] = V.turbo_cap, out[6] = V.rate, out[7] = V.awd, out[8] = V.air, out[9] = V.chassis->GetMass();
  out[10] = (float)V.nw, out[11] = (float)V.nseg;
}
#endif

extern "C" int car_planks(CarBody *out, int most) {
  int n = 0;
  if (!W) {   /* (the world gone: as they were) */
    for (; n < nkept_planks && n < most; n++) out[n] = kept_planks[n];
    return n;
  }
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < bridges[i].n && n < most; j++, n++) {
      b2Body *p = bridges[i].plank[j];
      out[n].x = p->GetPosition().x, out[n].y = p->GetPosition().y, out[n].a = p->GetAngle();
    }
  return n;
}

extern "C" int car_trees(CarBody *out, int *which, int most) {
  int n = 0;
  if (!W) {
    for (; n < nkept_trees && n < most; n++) out[n] = kept_trees[n], which[n] = kept_which[n];
    return n;
  }
  for (int i = 0; i < MAXTREE && n < most; i++) {
    if (trees[i].k < 0) continue;
    which[n] = trees[i].k;
    out[n].x = trees[i].b->GetPosition().x, out[n].y = trees[i].b->GetPosition().y, out[n].a = trees[i].b->GetAngle();
    n++;
  }
  return n;
}
