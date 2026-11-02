/* Rider's physics, see phys.h: Box2D (LiquidFun 1.1.0, src/Box2D) driven the way Corona SDK's physics library does it
 * (librtt/Rtt_LuaLibPhysics.cpp, Rtt_PhysicsWorld.cpp, Rtt_PhysicsContactListener.cpp, Rtt_DisplayObjectExtensions.cpp):
 * the same scale, defaults, joints, step and contact events. */
#include "phys.h"
#include <Box2D/Collision/Shapes/b2CircleShape.h>
#include <Box2D/Collision/Shapes/b2PolygonShape.h>
#include <Box2D/Dynamics/Contacts/b2Contact.h>
#include <Box2D/Dynamics/Joints/b2MouseJoint.h>
#include <Box2D/Dynamics/Joints/b2PrismaticJoint.h>
#include <Box2D/Dynamics/Joints/b2RevoluteJoint.h>
#include <Box2D/Dynamics/Joints/b2WeldJoint.h>
#include <Box2D/Dynamics/b2Body.h>
#include <Box2D/Dynamics/b2Fixture.h>
#include <Box2D/Dynamics/b2World.h>
#include <new>
#include <string.h>
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#ifndef PH_ARENA
#define PH_ARENA (72 * 1024)
#endif
#define SCALE 30.0f /* Corona's pixels a metre */
#define DEG 0.0174532925f

/* ---------------------------------------------------------------- the arena: first fit, blocks behind 8 bytes (their size) */
static ph_jmp *oom;
void ph_on_full(ph_jmp *j) { oom = j; }
typedef struct {
  uint32_t top, used, peak, pad;
#ifdef B2_BLOCK_HOOK
  int32_t dev_live[16], dev_chunks[16]; /* (tests, see below: kept in the arena, so copies of it carry them) */
#endif
} arena_head;
static union {
  uint8_t b[PH_ARENA];
  uint64_t align;
} arena;
#define HEAD ((arena_head *)arena.b)
#define FREE_BIT 1u

static void *arena_alloc(int32 size, void *) {
  uint32_t need = ((uint32_t)size + 8 + 7) & ~7u, at = sizeof(arena_head);
  while (at < HEAD->top) {
    uint32_t *h = (uint32_t *)(arena.b + at), sz = *h & ~FREE_BIT;
    if (*h & FREE_BIT) {
      /* join the free blocks after it */
      uint32_t nx = at + sz;
      while (nx < HEAD->top && (*(uint32_t *)(arena.b + nx) & FREE_BIT)) sz += *(uint32_t *)(arena.b + nx) & ~FREE_BIT, nx = at + sz;
      if (nx >= HEAD->top) { HEAD->top = at; break; } /* free up to the top: give it back */
      *h = sz | FREE_BIT;
      if (sz >= need) {
        if (sz - need >= 16) *(uint32_t *)(arena.b + at + need) = (sz - need) | FREE_BIT, sz = need;
        *h = sz, HEAD->used += sz;
        if (HEAD->used > HEAD->peak) HEAD->peak = HEAD->used;
        return h + 2;
      }
    }
    at += sz;
  }
  if (HEAD->top + need > PH_ARENA) {
    if (oom) PH_LONGJMP(*oom); /* (the game gives up this world) */
#ifdef HOST
    extern b2World *ph_world_for_debug;
    fprintf(stderr, "ph: arena full (%u bytes asked, %u in use; %d bodies, %d contacts, %d proxies)\n", (unsigned)size,
            (unsigned)HEAD->used, ph_world_for_debug->GetBodyCount(), ph_world_for_debug->GetContactCount(),
            ph_world_for_debug->GetProxyCount());
    abort();
#endif
    return 0;
  }
  uint32_t *h = (uint32_t *)(arena.b + HEAD->top);
  *h = need, HEAD->top += need, HEAD->used += need;
  if (HEAD->used > HEAD->peak) HEAD->peak = HEAD->used;
  return h + 2;
}
static void arena_free(void *p, void *) {
  if (!p) return;
  uint32_t *h = (uint32_t *)p - 2, sz = *h & ~FREE_BIT;
  HEAD->used -= sz;
  if ((uint8_t *)h + sz == arena.b + HEAD->top) HEAD->top = (uint32_t)((uint8_t *)h - arena.b);
  else *h = sz | FREE_BIT;
}

/* ---------------------------------------------------------------- contact events, as Corona's listener hands them on */
static const ph_listener *cbs;
static inline uint16_t tag_of(b2Fixture *f) { return (uint16_t)(uintptr_t)f->GetUserData(); }
struct Listener : public b2ContactListener {
  void BeginContact(b2Contact *c) {
    if (cbs && cbs->begin)
      cbs->begin(tag_of(c->GetFixtureA()), c->GetFixtureA()->GetFilterData().categoryBits, tag_of(c->GetFixtureB()),
                 c->GetFixtureB()->GetFilterData().categoryBits);
  }
  void EndContact(b2Contact *c) {
    if (cbs && cbs->end)
      cbs->end(tag_of(c->GetFixtureA()), c->GetFixtureA()->GetFilterData().categoryBits, tag_of(c->GetFixtureB()),
               c->GetFixtureB()->GetFilterData().categoryBits);
  }
  void PreSolve(b2Contact *c, const b2Manifold *) {
    if (cbs && cbs->pre) cbs->pre(tag_of(c->GetFixtureA()), tag_of(c->GetFixtureB()));
  }
  void PostSolve(b2Contact *c, const b2ContactImpulse *impulse) {
    if (!cbs || !cbs->post) return;
    /* "For the contact forces, we take the maximum within each set" */
    float force = 0;
    for (int32 i = 0; i < c->GetManifold()->pointCount; ++i) force = b2Max(force, impulse->normalImpulses[i]);
    cbs->post(tag_of(c->GetFixtureA()), c->GetFixtureA()->GetFilterData().categoryBits, tag_of(c->GetFixtureB()),
              c->GetFixtureB()->GetFilterData().categoryBits, force);
  }
};

#ifdef B2_BLOCK_HOOK
/* the tests: Box2D's blocks counted as the calculator's 32-bit build would size them (its chunks are kept: their
 * most at any time is what it holds) */
#define dev_live (HEAD->dev_live)
#define dev_chunks (HEAD->dev_chunks)
static int dev_size(int32 host) {
  switch (host) { /* (64-bit sizes on the left, the calculator's on the right) */
  case 200: return 164; /* b2Body */
  case 72: return 44;   /* b2Fixture */
  case 208: return 148; /* contacts */
  case 272: return 208; /* b2WeldJoint */
  case 320: return 252; /* b2PrismaticJoint */
  case 232: return 168; /* b2MouseJoint */
  case 288: return 228; /* b2RevoluteJoint */
  case 160: return 152; /* b2PolygonShape */
  case 24: return 20;   /* b2CircleShape */
  default: return host;
  }
}
extern "C" size_t ph_device_peak(void);
static void block_hook(int32 size, int32 d) {
  static const int cls[] = {16, 32, 64, 96, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640};
  int dv = dev_size(size), k = 0;
  while (cls[k] < dv) k++;
  dev_live[k] += d;
  int per = B2_CHUNK_SIZE / cls[k], need = (dev_live[k] + per - 1) / per;
  if (need > dev_chunks[k]) {
    dev_chunks[k] = need;
#ifdef PH_DEVICE_ARENA
    if (ph_device_peak() > PH_DEVICE_ARENA && oom) PH_LONGJMP(*oom); /* (the calculator would be out of memory here) */
#endif
  }
}
extern "C" void ph_device_classes(int *chunks) { for (int k = 0; k < 14; k++) chunks[k] = dev_chunks[k]; }
extern "C" size_t ph_device_used(void) { /* (the blocks in use, not the chunks kept) */
  static const int cls[] = {16, 32, 64, 96, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640};
  size_t n = 16 + 7816 - 7168 + B2_STACK_SIZE + 8 + B2_TREE_NODES * 36 + 8 + B2_CHUNK_ARRAY_INCREMENT * 8 + 8 + 512;
  for (int k = 0; k < 14; k++) n += (size_t)dev_live[k] * cls[k];
  return n;
}
extern "C" size_t ph_device_peak(void) {
  size_t n = 16 + 7816 - 7168 + B2_STACK_SIZE + 8 + B2_TREE_NODES * 36 + 8 + B2_CHUNK_ARRAY_INCREMENT * 8 + 8 + 512; /* (+ the broadphase's buffers) */
  for (int k = 0; k < 16; k++) n += (size_t)dev_chunks[k] * (B2_CHUNK_SIZE + 8);
  return n;
}
#endif

static b2World *world;
#ifdef HOST
b2World *ph_world_for_debug;
#endif
static b2Body *ground;
static Listener *listener;
static union {
  uint8_t b[sizeof(Listener)];
  void *align;
} listener_mem;

extern "C" {

void ph_world_new(void) {
  memset(HEAD, 0, sizeof *HEAD);
  HEAD->top = sizeof(arena_head);
#ifdef B2_BLOCK_HOOK
  b2_blockHook = block_hook;
#endif
  b2SetNumAllocs(0);
  b2SetAllocFreeCallbacks(arena_alloc, arena_free, 0);
  /* (no destructor: the arena is simply started again) */
  world = new (arena_alloc(sizeof(b2World), 0)) b2World(b2Vec2(0.0f, 9.8f));
  world->SetAllowSleeping(true);
#ifdef HOST
  ph_world_for_debug = world;
#endif
  listener = new (listener_mem.b) Listener();
  world->SetContactListener(listener);
  b2BodyDef bd;
  ground = world->CreateBody(&bd);
  cbs = 0;
}
size_t ph_used(void) { return HEAD->used; }
/* the arena's end never handed out yet: Box2D keeps its chunks for good, so this only goes down */
#if defined(B2_BLOCK_HOOK) && defined(PH_DEVICE_ARENA)
extern "C" size_t ph_device_peak(void);
size_t ph_tail(void) {
  size_t p = ph_device_peak();
  return p < PH_DEVICE_ARENA ? PH_DEVICE_ARENA - p : 0;
}
#else
size_t ph_tail(void) { return PH_ARENA - HEAD->top; }
#endif
/* what is free: the arena's end, its free blocks, and the free blocks in Box2D's chunks */
#if defined(B2_BLOCK_HOOK) && defined(PH_DEVICE_ARENA)
extern "C" size_t ph_device_used(void);
size_t ph_free(void) { /* (tests: as the calculator would see it) */
  size_t p = ph_device_used();
  return p < PH_DEVICE_ARENA ? PH_DEVICE_ARENA - p : 0;
}
#else
size_t ph_free(void) {
  return PH_ARENA - HEAD->top + (HEAD->top - sizeof(arena_head) - HEAD->used) + (size_t)world->GetFreeBlockBytes();
}
#endif
size_t ph_peak(void) { return HEAD->peak; }
size_t ph_arena_size(void) { return PH_ARENA; }
size_t ph_top(void) { return HEAD->top; }
void *ph_arena(void) { return arena.b; }
void *ph_scratch(size_t *n) {
  uint32_t at = HEAD->top < sizeof(arena_head) ? (uint32_t)sizeof(arena_head) : (HEAD->top + 7) & ~7u;
  *n = at < PH_ARENA ? PH_ARENA - at : 0;
  return arena.b + at;
}
ph_body *ph_ground(void) { return (ph_body *)ground; }
int ph_proxies(void) { return world->GetProxyCount(); }
int ph_contacts(void) { return world->GetContactCount(); }
int ph_bodies(void) { return world->GetBodyCount(); }
void ph_listen(const ph_listener *l) { cbs = l; }

/* Corona's CreateBody: a dynamic body, moved and turned, then given its type */
ph_body *ph_body_new(int type, float x, float y, float rot) {
  b2BodyDef bd;
  bd.type = b2_dynamicBody;
  b2Body *b = world->CreateBody(&bd);
  b->SetTransform(b2Vec2(x / SCALE, y / SCALE), rot * DEG);
  if (type != PH_DYNAMIC) b->SetType(type == PH_STATIC ? b2_staticBody : b2_kinematicBody);
  return (ph_body *)b;
}
void ph_body_free(ph_body *b) { world->DestroyBody((b2Body *)b); }

static void fixdef(b2FixtureDef *fd, const ph_fixdef *d) {
  fd->density = d->density > 0 ? d->density : 0.01f; /* "previous default was zero, but that did odd things" */
  fd->friction = d->friction >= 0 ? d->friction : 0.3f;
  fd->restitution = d->bounce >= 0 ? d->bounce : 0.5f;
  fd->isSensor = d->sensor;
  fd->filter.categoryBits = d->cat, fd->filter.maskBits = d->mask, fd->filter.groupIndex = 0;
  fd->userData = (void *)(uintptr_t)d->tag;
}
ph_fixture *ph_poly(ph_body *b, const float *xy, int n, const ph_fixdef *d) {
  if (n < 3 || n > b2_maxPolygonVertices) return 0;
  b2Vec2 v[b2_maxPolygonVertices];
  for (int i = 0; i < n; i++) v[i].Set(xy[2 * i] / SCALE, xy[2 * i + 1] / SCALE);
  b2PolygonShape s;
  if (!s.Set(v, n)) return 0; /* Corona looks at the answer instead of asserting */
  b2FixtureDef fd;
  fixdef(&fd, d), fd.shape = &s;
  return (ph_fixture *)((b2Body *)b)->CreateFixture(&fd);
}
ph_fixture *ph_circle(ph_body *b, float x, float y, float r, const ph_fixdef *d) {
  b2CircleShape s;
  s.m_radius = r / SCALE, s.m_p.Set(x / SCALE, y / SCALE);
  b2FixtureDef fd;
  fixdef(&fd, d), fd.shape = &s;
  return (ph_fixture *)((b2Body *)b)->CreateFixture(&fd);
}
void ph_fixture_free(ph_body *b, ph_fixture *f) { ((b2Body *)b)->DestroyFixture((b2Fixture *)f); }
void ph_free_tag(ph_body *b, uint16_t tag) {
  b2Fixture *f = ((b2Body *)b)->GetFixtureList();
  while (f) {
    b2Fixture *n = f->GetNext();
    if (tag_of(f) == tag) ((b2Body *)b)->DestroyFixture(f);
    f = n;
  }
}

/* ---------------------------------------------------------------- joints, as physics.newJoint makes them */
ph_joint *ph_weld(ph_body *a, ph_body *b, float x, float y) {
  b2WeldJointDef jd;
  jd.Initialize((b2Body *)a, (b2Body *)b, b2Vec2(x / SCALE, y / SCALE));
  return (ph_joint *)world->CreateJoint(&jd);
}
ph_joint *ph_piston(ph_body *a, ph_body *b, float x, float y, float ax, float ay) {
  b2PrismaticJointDef jd;
  b2Vec2 axis(ax, ay);
  axis.Normalize();
  jd.Initialize((b2Body *)a, (b2Body *)b, b2Vec2(x / SCALE, y / SCALE), axis);
  return (ph_joint *)world->CreateJoint(&jd);
}
void ph_piston_limits(ph_joint *j, float lo, float hi) {
  b2PrismaticJoint *p = (b2PrismaticJoint *)j;
  p->EnableLimit(true);
  p->SetLimits(lo / SCALE, hi / SCALE);
}
ph_joint *ph_pivot(ph_body *a, ph_body *b, float x, float y) {
  b2RevoluteJointDef jd;
  jd.Initialize((b2Body *)a, (b2Body *)b, b2Vec2(x / SCALE, y / SCALE));
  return (ph_joint *)world->CreateJoint(&jd);
}
ph_joint *ph_touch(ph_body *b, float x, float y) {
  b2MouseJointDef jd;
  jd.bodyA = ground, jd.bodyB = (b2Body *)b;
  jd.target.Set(x / SCALE, y / SCALE);
  jd.maxForce = 1000.f * ((b2Body *)b)->GetMass();
  ((b2Body *)b)->SetAwake(true);
  return (ph_joint *)world->CreateJoint(&jd);
}
void ph_touch_tune(ph_joint *j, float freq, float damping) {
  ((b2MouseJoint *)j)->SetFrequency(freq);
  ((b2MouseJoint *)j)->SetDampingRatio(damping);
}
void ph_touch_set(ph_joint *j, float x, float y) { ((b2MouseJoint *)j)->SetTarget(b2Vec2(x / SCALE, y / SCALE)); }
void ph_touch_get(const ph_joint *j, float *x, float *y) {
  const b2Vec2 &t = ((const b2MouseJoint *)j)->GetTarget();
  *x = t.x * SCALE, *y = t.y * SCALE;
}
void ph_joint_free(ph_joint *j) { world->DestroyJoint((b2Joint *)j); }

/* ---------------------------------------------------------------- a body, as its display object's properties */
void ph_get(const ph_body *b, float *x, float *y, float *rot) {
  const b2Body *k = (const b2Body *)b;
  *x = k->GetPosition().x * SCALE, *y = k->GetPosition().y * SCALE;
  if (rot) *rot = k->GetAngle() / DEG;
}
void ph_vel(const ph_body *b, float *vx, float *vy) {
  b2Vec2 v = ((const b2Body *)b)->GetLinearVelocity();
  *vx = v.x * SCALE, *vy = v.y * SCALE;
}
float ph_avel(const ph_body *b) { return ((const b2Body *)b)->GetAngularVelocity() / DEG; }
void ph_set_vel(ph_body *b, float vx, float vy) { ((b2Body *)b)->SetLinearVelocity(b2Vec2(vx / SCALE, vy / SCALE)); }
void ph_set_avel(ph_body *b, float w) { ((b2Body *)b)->SetAngularVelocity(w * DEG); }
void ph_set_pos(ph_body *b, float x, float y) { ((b2Body *)b)->SetTransform(b2Vec2(x / SCALE, y / SCALE), ((b2Body *)b)->GetAngle()); }
void ph_set_rot(ph_body *b, float rot) { ((b2Body *)b)->SetTransform(((b2Body *)b)->GetPosition(), rot * DEG); }
void ph_set_fixed_rot(ph_body *b, bool on) { ((b2Body *)b)->SetFixedRotation(on); }
bool ph_fixed_rot(const ph_body *b) { return ((const b2Body *)b)->IsFixedRotation(); }
void ph_set_damping(ph_body *b, float d) { ((b2Body *)b)->SetLinearDamping(d); }
void ph_set_gravity(ph_body *b, float scale) { ((b2Body *)b)->SetGravityScale(scale); }
void ph_set_bullet(ph_body *b, bool on) { ((b2Body *)b)->SetBullet(on); }
void ph_set_sensor(ph_body *b, bool on) {
  for (b2Fixture *f = ((b2Body *)b)->GetFixtureList(); f; f = f->GetNext()) f->SetSensor(on);
}
void ph_torque(ph_body *b, float t) { ((b2Body *)b)->ApplyTorque(t, true); }
float ph_mass(const ph_body *b) { return ((const b2Body *)b)->GetMass(); }
bool ph_awake(const ph_body *b) { return ((const b2Body *)b)->IsAwake(); }
bool ph_bounds(const ph_body *b, float *x0, float *y0, float *x1, float *y1) {
  const b2Body *k = (const b2Body *)b;
  bool any = false;
  b2AABB box;
  for (const b2Fixture *f = k->GetFixtureList(); f; f = f->GetNext()) {
    b2AABB a;
    f->GetShape()->ComputeAABB(&a, k->GetTransform(), 0);
    if (!any) box = a, any = true;
    else box.Combine(a);
  }
  if (any) *x0 = box.lowerBound.x * SCALE, *y0 = box.lowerBound.y * SCALE, *x1 = box.upperBound.x * SCALE, *y1 = box.upperBound.y * SCALE;
  return any;
}

/* two polygons (in the world's pixels) touching as Box2D's contacts count it: a manifold with points */
bool ph_polys_touch(const float *a, int na, const float *b, int nb) {
  if (na < 3 || nb < 3 || na > b2_maxPolygonVertices || nb > b2_maxPolygonVertices) return false;
  b2Vec2 va[b2_maxPolygonVertices], vb[b2_maxPolygonVertices];
  for (int i = 0; i < na; i++) va[i].Set(a[2 * i] / SCALE, a[2 * i + 1] / SCALE);
  for (int i = 0; i < nb; i++) vb[i].Set(b[2 * i] / SCALE, b[2 * i + 1] / SCALE);
  b2PolygonShape sa, sb;
  if (!sa.Set(va, na) || !sb.Set(vb, nb)) return false;
  b2Transform id;
  id.SetIdentity();
  b2Manifold m;
  b2CollidePolygons(&m, &sa, id, &sb, id);
  return m.pointCount > 0;
}

/* Corona's frame-based step: dt = 1/fps (60, config.lua), 8 velocity and 3 position iterations */
void ph_step(void) { world->Step(1.0f / 60, 8, 3); }

/* C++ without its library: the few pieces the compiler asks for */
#ifndef HOST
void __cxa_pure_virtual(void) {
  for (;;) {
  }
}
#endif
}
#ifndef HOST
void operator delete(void *) noexcept {}
void operator delete(void *, unsigned int) noexcept {}
#endif
