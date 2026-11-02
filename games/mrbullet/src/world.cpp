/* The physics (world.h): Box2D 2.3.1 as Mr Bullet sets it up.
 *
 * As in the game: gravity 9.81 m/s2, steps of 1/60 s with 18 velocity and 14 position iterations, sleeping,
 * warm starting and continuous collision, 60 of the game's units a metre. Collision categories as its matrix:
 * category 0 meets nothing, characters' pieces never meet each other, nor bullets each other. */
#include <new>
#include <math.h>
#include <Box2D/Dynamics/b2World.h>
#include <Box2D/Dynamics/b2Body.h>
#include <Box2D/Dynamics/b2Fixture.h>
#include <Box2D/Dynamics/b2WorldCallbacks.h>
#include <Box2D/Dynamics/Contacts/b2Contact.h>
#include <Box2D/Dynamics/Joints/b2RevoluteJoint.h>
#include <Box2D/Dynamics/Joints/b2RopeJoint.h>
#include <Box2D/Collision/Shapes/b2ChainShape.h>
#include <Box2D/Collision/Shapes/b2CircleShape.h>
#include <Box2D/Collision/Shapes/b2PolygonShape.h>
#include "world.h"

#define U (1.0f / 60.0f)   /* metres a unit */

/* (Box2D's classes have virtual destructors, which name delete: the world itself is placed in static memory) */
void operator delete(void *p) noexcept { free(p); }
void operator delete(void *p, unsigned int) noexcept { free(p); }
void operator delete(void *p, unsigned long) noexcept { free(p); }
#ifndef HOST
extern "C" void __cxa_pure_virtual(void) {
  for (;;) {}
}
#endif

static union { char mem[sizeof(b2World)]; double align; } world_mem;
static b2World *world;
static b2Body *bodies[W_BODIES];
static int body_obj[W_BODIES];
static b2Joint *joints[W_JOINTS];
static b2Body *ground;

static inline b2Vec2 M(float x, float y) { return b2Vec2(x * U, -y * U); }

class Listener : public b2ContactListener {
 public:
  void BeginContact(b2Contact *c) {
    b2Body *a = c->GetFixtureA()->GetBody(), *b = c->GetFixtureB()->GetBody();
    int ia = (int)(intptr_t)a->GetUserData() - 1, ib = (int)(intptr_t)b->GetUserData() - 1;
    if (ia < 0 || ib < 0) return;
    b2WorldManifold m;
    c->GetWorldManifold(&m);
    b2Vec2 p = c->GetManifold()->pointCount ? m.points[0] : 0.5f * (a->GetWorldCenter() + b->GetWorldCenter());
    b2Vec2 dv = b->GetLinearVelocityFromWorldPoint(p) - a->GetLinearVelocityFromWorldPoint(p);
    float speed = c->GetManifold()->pointCount ? -b2Dot(dv, m.normal) : dv.Length();
    if (speed < 0) speed = -speed;
    game_contact(body_obj[ia], body_obj[ib], ia, ib, speed / U, p.x / U, -p.y / U);
  }
  void PreSolve(b2Contact *c, const b2Manifold *old) {
    (void)old;
    int ia = (int)(intptr_t)c->GetFixtureA()->GetBody()->GetUserData() - 1;
    int ib = (int)(intptr_t)c->GetFixtureB()->GetBody()->GetUserData() - 1;
    if (ia >= 0 && ib >= 0 && !game_contact_enabled(body_obj[ia], body_obj[ib])) c->SetEnabled(false);
  }
};
static union { char mem[sizeof(Listener)]; void *align; } listener_mem;
static Listener *listener;

extern "C" bool world_begin(void) {
  world_end();
  world = new (world_mem.mem) b2World(b2Vec2(0.0f, -9.81f));
  world->SetAllowSleeping(true);
  world->SetWarmStarting(true);
  world->SetContinuousPhysics(true);
  if (!listener) listener = new (listener_mem.mem) Listener;
  world->SetContactListener(listener);
  return true;
}

extern "C" void world_end(void) {
  if (world) world->~b2World();   /* (gives back all its blocks) */
  world = 0;
  ground = 0;
  for (int i = 0; i < W_BODIES; i++) bodies[i] = 0;
  for (int i = 0; i < W_JOINTS; i++) joints[i] = 0;
}

extern "C" int world_body(int type, float x, float y, float angle, int obj, bool bullet, bool fixed_rotation,
                          float gravity) {
  if (!world) return -1;
  int i = 0;
  while (i < W_BODIES && bodies[i]) i++;
  if (i == W_BODIES) return -1;
  b2BodyDef bd;
  bd.type = type == WB_STATIC ? b2_staticBody : type == WB_KINEMATIC ? b2_kinematicBody : b2_dynamicBody;
  bd.position = M(x, y);
  bd.angle = -angle;
  bd.linearDamping = type == WB_DYNAMIC ? 0.5f : 0.0f;   /* every dynamic body: (0.5, 0) */
  bd.bullet = bullet;
  bd.fixedRotation = fixed_rotation;
  bd.gravityScale = gravity;
  bd.userData = (void *)(intptr_t)(i + 1);
  b2Body *b = world->CreateBody(&bd);
  if (!b) return -1;
  bodies[i] = b;
  body_obj[i] = obj;
  return i;
}

static void filter_of(b2FixtureDef *fd, const WFix *f) {
  fd->density = f->density;
  fd->friction = f->friction;
  fd->restitution = f->restitution;
  fd->isSensor = f->sensor != 0;
  int c = f->category;
  fd->filter.categoryBits = (uint16)(1u << c);
  uint16 mask = 0xFFFF;
  if (c == WC_NONE) mask = 0;
  else mask &= (uint16)~1u;   /* (category 0 meets nothing) */
  if (c == WC_CHARACTER) mask &= (uint16)~(1u << WC_CHARACTER);
  if (c == WC_BULLET) mask &= (uint16)~(1u << WC_BULLET | 1u << WC_CHARACTER);   /* (bullets go through
     characters: game.c finds them along their path) */
  if (c == WC_CHARACTER) mask &= (uint16)~(1u << WC_BULLET);
  fd->filter.maskBits = mask;
}

static bool add_shape(int b, const b2Shape *s, const WFix *f) {
  if (b < 0 || b >= W_BODIES || !bodies[b]) return false;
  b2FixtureDef fd;
  fd.shape = s;
  filter_of(&fd, f);
  return bodies[b]->CreateFixture(&fd) != 0;
}

extern "C" bool world_poly(int b, const float *xy, int n, const WFix *f) {
  if (n < 3 || n > b2_maxPolygonVertices) return false;
  b2Vec2 v[b2_maxPolygonVertices];
  float area = 0;
  for (int i = 0; i < n; i++) {
    v[i] = b2Vec2(xy[2 * i] * U, -xy[2 * i + 1] * U);
    int j = i ? i - 1 : n - 1;
    area += xy[2 * j] * xy[2 * i + 1] - xy[2 * i] * xy[2 * j + 1];
  }
  if (fabsf(area) < 2.0f) return false;   /* (a sliver Box2D would turn into its 2 m box) */
  b2PolygonShape p;
  p.Set(v, n);
  if (p.m_count < 3) return false;
  return add_shape(b, &p, f);
}

extern "C" bool world_chain(int b, float *xy, int n, const WFix *f) {
  /* the points become Box2D's in place (b2Vec2: two floats) */
  b2Vec2 *v = (b2Vec2 *)(void *)xy;
  int m = 0;
  for (int i = 0; i < n; i++) {
    b2Vec2 p(xy[2 * i] * U, -xy[2 * i + 1] * U);
    /* (the rounded corners' points 3 units apart at most: fewer edges, each a box in the broad phase) */
    if (m && b2DistanceSquared(p, v[m - 1]) < 9.0f * U * U) continue;
    v[m++] = p;
  }
  while (m > 3 && b2DistanceSquared(v[0], v[m - 1]) < 0.25f * U * U) m--;
  if (m < 3) return false;
  b2ChainShape c;
  c.CreateLoop(v, m);
  return add_shape(b, &c, f);   /* (the fixture keeps its own copy of the points) */
}

extern "C" bool world_box(int b, float cx, float cy, float hw, float hh, const WFix *f) {
  b2PolygonShape p;
  p.SetAsBox(hw * U, hh * U, b2Vec2(cx * U, -cy * U), 0);
  return add_shape(b, &p, f);
}

extern "C" bool world_circle(int b, float cx, float cy, float r, const WFix *f) {
  b2CircleShape c;
  c.m_p.Set(cx * U, -cy * U);
  c.m_radius = r * U;
  return add_shape(b, &c, f);
}

extern "C" void world_destroy(int b) {
  if (!world || b < 0 || b >= W_BODIES || !bodies[b]) return;
  /* its joints go with it */
  for (b2JointEdge *e = bodies[b]->GetJointList(); e; e = e->next)
    for (int j = 0; j < W_JOINTS; j++)
      if (joints[j] == e->joint) joints[j] = 0;
  world->DestroyBody(bodies[b]);
  bodies[b] = 0;
}

extern "C" bool world_alive(int b) { return b >= 0 && b < W_BODIES && bodies[b]; }
extern "C" int world_obj(int b) { return world_alive(b) ? body_obj[b] : -1; }

static b2Body *ground_body(void) {
  if (!ground) {
    b2BodyDef bd;
    ground = world->CreateBody(&bd);
  }
  return ground;
}

static int add_joint(b2Joint *j) {
  if (!j) return -1;
  for (int i = 0; i < W_JOINTS; i++)
    if (!joints[i]) {
      joints[i] = j;
      return i;
    }
  world->DestroyJoint(j);
  return -1;
}

static b2Body *body_or_ground(int b) { return world_alive(b) ? bodies[b] : ground_body(); }

extern "C" int world_revolute(int a, int b, float x, float y, bool limit, float lo, float hi, bool motor, float speed,
                              float torque) {
  if (!world) return -1;
  b2Body *ba = body_or_ground(a), *bb = body_or_ground(b);
  if (ba == bb) return -1;
  b2RevoluteJointDef jd;
  jd.Initialize(ba, bb, M(x, y));
  jd.enableLimit = limit;
  jd.lowerAngle = lo;
  jd.upperAngle = hi;
  jd.enableMotor = motor;
  jd.motorSpeed = speed;
  jd.maxMotorTorque = torque;
  return add_joint(world->CreateJoint(&jd));
}

extern "C" int world_rope(int a, int b, float ax, float ay, float bx, float by, float length) {
  if (!world) return -1;
  b2Body *ba = body_or_ground(a), *bb = body_or_ground(b);
  if (ba == bb) return -1;
  b2RopeJointDef jd;
  jd.bodyA = ba;
  jd.bodyB = bb;
  jd.localAnchorA = ba->GetLocalPoint(M(ax, ay));
  jd.localAnchorB = bb->GetLocalPoint(M(bx, by));
  jd.maxLength = length * U;
  jd.collideConnected = true;
  return add_joint(world->CreateJoint(&jd));
}

extern "C" void world_destroy_joint(int j) {
  if (!world || j < 0 || j >= W_JOINTS || !joints[j]) return;
  world->DestroyJoint(joints[j]);
  joints[j] = 0;
}
extern "C" bool world_joint_alive(int j) { return j >= 0 && j < W_JOINTS && joints[j]; }
extern "C" void world_joint_anchors(int j, float *ax, float *ay, float *bx, float *by) {
  b2Vec2 a = joints[j]->GetAnchorA(), b = joints[j]->GetAnchorB();
  *ax = a.x / U, *ay = -a.y / U, *bx = b.x / U, *by = -b.y / U;
}
extern "C" void world_set_limits(int j, bool on, float lo, float hi) {
  if (!world_joint_alive(j) || joints[j]->GetType() != e_revoluteJoint) return;
  b2RevoluteJoint *r = (b2RevoluteJoint *)joints[j];
  r->EnableLimit(on);
  if (on) r->SetLimits(lo, hi);
}

extern "C" void world_step(void) {
  if (world) world->Step(W_STEP, 18, 14);
}

extern "C" void world_xf(int b, float *x, float *y, float *s, float *c) {
  const b2Transform &t = bodies[b]->GetTransform();
  *x = t.p.x / U, *y = -t.p.y / U, *s = -t.q.s, *c = t.q.c;
}
extern "C" float world_angle(int b) { return -bodies[b]->GetAngle(); }
extern "C" void world_velocity(int b, float *vx, float *vy) {
  b2Vec2 v = bodies[b]->GetLinearVelocity();
  *vx = v.x / U, *vy = -v.y / U;
}
extern "C" void world_set_velocity(int b, float vx, float vy) { bodies[b]->SetLinearVelocity(b2Vec2(vx * U, -vy * U)); }
extern "C" void world_set_angular(int b, float w) { bodies[b]->SetAngularVelocity(-w); }
extern "C" float world_angular(int b) { return -bodies[b]->GetAngularVelocity(); }
extern "C" void world_set_transform(int b, float x, float y, float angle) { bodies[b]->SetTransform(M(x, y), -angle); }
extern "C" void world_impulse(int b, float ix, float iy, float px, float py) {
  bodies[b]->ApplyLinearImpulse(b2Vec2(ix, -iy), M(px, py), true);
}
extern "C" void world_angular_impulse(int b, float i) { bodies[b]->ApplyAngularImpulse(-i, true); }
extern "C" float world_mass(int b) { return bodies[b]->GetMass(); }
extern "C" bool world_awake(int b) { return bodies[b]->IsAwake(); }
extern "C" bool world_dynamic(int b) { return bodies[b]->GetType() == b2_dynamicBody; }
extern "C" void world_set_type(int b, int type) {
  bodies[b]->SetType(type == WB_STATIC ? b2_staticBody : type == WB_KINEMATIC ? b2_kinematicBody : b2_dynamicBody);
}
extern "C" void world_wake(int b) { bodies[b]->SetAwake(true); }
extern "C" void world_set_fixed_rotation(int b, bool on) { bodies[b]->SetFixedRotation(on); }

class Ray : public b2RayCastCallback {
 public:
  unsigned mask;
  b2Fixture *hit;
  b2Vec2 p, n;
  float32 ReportFixture(b2Fixture *f, const b2Vec2 &point, const b2Vec2 &normal, float32 fraction) {
    if (f->IsSensor() || !(mask & f->GetFilterData().categoryBits)) return -1;
    hit = f, p = point, n = normal;
    return fraction;
  }
};
extern "C" int world_ray(float x0, float y0, float x1, float y1, unsigned mask, float *hx, float *hy, float *nx,
                         float *ny) {
  if (!world) return -1;
  Ray r;
  r.mask = mask, r.hit = 0;
  b2Vec2 a = M(x0, y0), b = M(x1, y1);
  if ((b - a).LengthSquared() < 1e-12f) return -1;
  world->RayCast(&r, a, b);
  if (!r.hit) return -1;
  *hx = r.p.x / U, *hy = -r.p.y / U, *nx = r.n.x, *ny = -r.n.y;
  return (int)(intptr_t)r.hit->GetBody()->GetUserData() - 1;
}

class RayAll : public b2RayCastCallback {
 public:
  unsigned mask;
  int *out, max, n;
  float *xs, *ys;
  float32 ReportFixture(b2Fixture *f, const b2Vec2 &point, const b2Vec2 &normal, float32 fraction) {
    (void)normal, (void)fraction;
    if (f->IsSensor() || !(mask & f->GetFilterData().categoryBits)) return -1;
    int b = (int)(intptr_t)f->GetBody()->GetUserData() - 1;
    if (b < 0) return 1;
    for (int i = 0; i < n; i++)
      if (out[i] == b) return 1;
    if (n < max) out[n] = b, xs[n] = point.x / U, ys[n] = -point.y / U, n++;
    return n < max ? 1 : 0;
  }
};
extern "C" int world_ray_all(float x0, float y0, float x1, float y1, unsigned mask, int *out, float *xs, float *ys,
                             int max) {
  if (!world) return 0;
  RayAll r;
  r.mask = mask, r.out = out, r.max = max, r.n = 0, r.xs = xs, r.ys = ys;
  b2Vec2 a = M(x0, y0), b = M(x1, y1);
  if ((b - a).LengthSquared() < 1e-12f) return 0;
  world->RayCast(&r, a, b);
  return r.n;
}

class Query : public b2QueryCallback {
 public:
  int *out, max, n;
  bool ReportFixture(b2Fixture *f) {
    int b = (int)(intptr_t)f->GetBody()->GetUserData() - 1;
    if (b < 0) return true;
    for (int i = 0; i < n; i++)
      if (out[i] == b) return true;
    if (n < max) out[n++] = b;
    return n < max;
  }
};
extern "C" int world_query(float x0, float y0, float x1, float y1, int *out, int max) {
  if (!world) return 0;
  Query q;
  q.out = out, q.max = max, q.n = 0;
  b2AABB box;
  box.lowerBound = b2Vec2(fminf(x0, x1) * U, -fmaxf(y0, y1) * U);
  box.upperBound = b2Vec2(fmaxf(x0, x1) * U, -fminf(y0, y1) * U);
  world->QueryAABB(&q, box);
  return q.n;
}

extern "C" int world_bodies(void) { return world ? world->GetBodyCount() : 0; }
extern "C" int world_contacts(void) { return world ? world->GetContactCount() : 0; }
#ifdef HOST
#include <stdio.h>
extern "C" void world_debug(void) {
  int n = 0, touching = 0;
  for (b2Contact *c = world->GetContactList(); c; c = c->GetNext()) {
    n++;
    if (c->IsTouching()) touching++;
    if (n < 40 && c->IsTouching()) printf("contact %d-%d types %d %d touching %d\n", (int)(intptr_t)c->GetFixtureA()->GetBody()->GetUserData(),
                       (int)(intptr_t)c->GetFixtureB()->GetBody()->GetUserData(), c->GetFixtureA()->GetType(),
                       c->GetFixtureB()->GetType(), c->IsTouching());
  }
  printf("contacts %d touching %d bodies %d proxies %d\n", n, touching, world->GetBodyCount(), world->GetProxyCount());
}
#endif
