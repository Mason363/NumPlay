/* The Final Earth 2: who works, lives and studies where
 * (simulation.JobAssigner, HouseAssigner, SchoolAssigner) and the search for
 * the best place near a building (pathfinder.PermanentFinder). */
#include "sim.h"
#include "bld.h"
#include "scratch.h"
#include "classes.h"
#define prio (scratch->prio)
#define seen (scratch->seen)

/* ---------------------------------------------------------------- PermanentFinder.query
 * a search over the buildings' neighbours, teleporters, landing sites,
 * elevators and worlds' surfaces from a permanent (or a world's x), for the
 * goal with the highest niceness (the first goal reached when there is no
 * niceness), stopping at one as nice as best_expected */
typedef bool (*IsGoal)(const Perm *p, const void *ctx);
typedef float (*Niceness)(const Perm *p);
#define no_elevators (scratch->no_elevators)   /* dontCheckElevators */
static int finder_queries;               /* this tick (canPerformQuery) */
static int finder_limit_for;

static void finder_relax(uint16_t to, float np, float max_distance) {
  if (seen[to] == 1) {
    if (np < prio[to]) {
      prio[to] = np;
      heap_up(heap_index(to));
    }
  } else if (seen[to] != 2 && (max_distance < 0 || np <= max_distance)) {
    seen[to] = 1;
    prio[to] = np;
    heap_push(to);
  }
}

uint16_t finder_query(uint16_t from, int from_world, float from_x, IsGoal is_goal, const void *ctx, Niceness niceness,
                      float max_distance, const Cit *citizen, float best_expected) {
  memset(seen, 0, sizeof seen);
  memset(no_elevators, 0, sizeof no_elevators);
  heap_n = 0;
  finder_queries++;
  if (from) {
    prio[from] = 0;
    seen[from] = 1;
    heap_push(from);
  } else {
    World *w = &worlds[from_world];
    for (int x = 0; x < w->ncols; x++) {
      uint16_t b = perm_at(from_world, x, 0);
      if (!b) continue;
      float pr = fabsf(perm_city_x(P(b)) - from_x);
      if (max_distance >= 0 && pr > max_distance) continue;
      prio[b] = pr;
      seen[b] = 1;
      heap_push(b);
    }
  }
  float max_nice = -10000;
  uint16_t best = NONE;
  bool tp_done = !operating_cost.teleportersEnabled;
  static uint8_t ls_done[64];
  memset(ls_done, 0, sizeof ls_done);
  uint16_t private_tp = (citizen && citizen->home && bld_has_private_teleporter(P(citizen->home))) ? citizen->home : NONE;
  while (heap_n) {
    uint16_t cur = heap_pop();
    Perm *p = &perms[cur];
    seen[cur] = 2;
    if (is_goal(p, ctx)) {
      if (niceness) {
        float n = niceness(p);
        if (n >= best_expected) return cur;
        if (n > max_nice) best = cur, max_nice = n;
      } else {
        return cur;
      }
    }
    if (is_building(p)) {
      if (p->left) finder_relax(p->left, prio[cur] + 20, max_distance);
      if (p->right) finder_relax(p->right, prio[cur] + 20, max_distance);
      if (p->top) finder_relax(p->top, prio[cur] + 20, max_distance);
      if (p->bottom) finder_relax(p->bottom, prio[cur] + 20, max_distance);
    }
    bool is_private = cur == private_tp;
    if ((is_a(p, B_TELEPORTER) || is_private) && !tp_done) {
      float r = rnd_range(0, 2);
      int len = 0;
      for (int j = 1; j < MAX_PERMS; j++)
        if ((perms[j].flags & PF_USED) && is_a(&perms[j], B_TELEPORTER)) len++;
      float inv = len ? 2.0f / len : 0;
      int t = 0;
      for (int j = 1; j < MAX_PERMS; j++)
        if ((perms[j].flags & PF_USED) && is_a(&perms[j], B_TELEPORTER))
          finder_relax((uint16_t)j, prio[cur] + 9 + fmodf(r + t++ * inv, 2.0f), max_distance);
      if (!is_private && private_tp) finder_relax(private_tp, prio[cur] + 10, max_distance);
      tp_done = true;
    } else if (is_a(p, B_LANDINGSITE) && landing_group_of[cur] && landing_group_of[cur] <= 64 && !ls_done[landing_group_of[cur] - 1]) {
      for (int j = 1; j < MAX_PERMS; j++)
        if (landing_group_of[j] == landing_group_of[cur])
          finder_relax((uint16_t)j, prio[cur] + bld_flying_distance(P((uint16_t)j), p), max_distance);
      ls_done[landing_group_of[cur] - 1] = 1;
    } else if (is_a(p, B_ELEVATOR) && !no_elevators[cur]) {
      int h = col_stack_height(p->world, p->col);
      for (int y = 0; y < h; y++) {
        Perm *e = P(perm_at(p->world, p->col, y));
        if (!e || !is_a(e, B_ELEVATOR)) continue;
        finder_relax(pid(e), prio[cur] + abs((int)e->row - (int)p->row), max_distance);
        no_elevators[pid(e)] = 1;
      }
    }
    if (p->row == 0) {
      World *w = &worlds[p->world];
      for (int x = 0; x < w->ncols; x++) {
        uint16_t b = perm_at(p->world, x, 0);
        if (b) finder_relax(b, prio[cur] + fabsf(20.0f * (x - p->col)), max_distance);
      }
    }
  }
  return best;
}
static uint16_t finder_query_for_citizen(const Cit *c, IsGoal is_goal, const void *ctx, Niceness niceness, float max_distance, bool quick) {
  if (c->inPermanent) return finder_query(c->inPermanent, 0, 0, is_goal, ctx, niceness, max_distance, quick ? NULL : c, 1e8f);
  return finder_query(NONE, c->onWorld, c->relativeX, is_goal, ctx, niceness, max_distance, quick ? NULL : c, 1e8f);
}

/* quickQueryForCitizen: the result is kept for expiry_hours for queries from about the same place */
#define QCACHE 48
static struct { uint8_t world, x, y, key; uint16_t perm; float expiry; } qcache[QCACHE];
static int qcache_next;
uint16_t finder_quick_query(const Cit *c, IsGoal is_goal, const void *ctx, uint8_t key, float expiry_hours, Niceness niceness,
                            float max_distance) {
  int xi, yi;
  if (c->inPermanent) xi = P(c->inPermanent)->col, yi = P(c->inPermanent)->row / 5;
  else xi = (int)c->relativeX / 20, yi = 0;
  float now = (float)city.timeSinceStart;
  for (int i = 0; i < QCACHE; i++) {
    if (qcache[i].key != key || qcache[i].world != c->onWorld || qcache[i].x != xi || qcache[i].y != yi) continue;
    if (now < qcache[i].expiry) return qcache[i].perm;
    if (qcache[i].perm && (perms[qcache[i].perm].flags & PF_USED) && is_goal(P(qcache[i].perm), ctx)) {
      qcache[i].expiry += expiry_hours * 60;
      return qcache[i].perm;
    }
    uint16_t f = finder_query_for_citizen(c, is_goal, ctx, niceness, max_distance, true);
    qcache[i].perm = f;
    qcache[i].expiry = now + expiry_hours * 60;
    return f;
  }
  uint16_t f = finder_query_for_citizen(c, is_goal, ctx, niceness, max_distance, true);
  int i = qcache_next++ % QCACHE;
  qcache[i].key = key, qcache[i].world = c->onWorld, qcache[i].x = (uint8_t)xi, qcache[i].y = (uint8_t)yi;
  qcache[i].perm = f;
  qcache[i].expiry = now + expiry_hours * 60;
  return f;
}
void finder_invalidate(void) { memset(qcache, 0, sizeof qcache); }
void finder_pre_update(void) {
  finder_queries = 0;
  if (finder_limit_for > 0) finder_limit_for--;
}
bool finder_can_query(void) { return finder_limit_for > 0 ? finder_queries < 10 : true; }
void finder_request_limiting(bool strict_first) {
  finder_limit_for = 120;
  if (strict_first) finder_queries = 10;
}

/* ---------------------------------------------------------------- homes */
static uint16_t cit_shard(const Cit *c) { return c->inPermanent ? P(c->inPermanent)->shard : worlds[c->onWorld].surfaceShard; }
static int remaining_home(const Perm *p) { return bld_residents(p) - p->nresidents; }
static bool home_with_room(const Perm *p) {
  if (!is_a(p, B_HOUSE) || remaining_home(p) <= 0) return false;
  int fixed = bld_fixed_capacity_for_workers(p);
  return fixed == 0 || p->nworkers >= fixed;
}
static bool goal_home_shard(const Perm *p, const void *ctx) { return home_with_room(p) && p->shard == *(const uint16_t *)ctx; }
static bool goal_home_shard_or_tp(const Perm *p, const void *ctx) {
  return goal_home_shard(p, ctx) || (is_a(p, B_TELEPORTER) && operating_cost.teleportersEnabled);
}
static float home_niceness(const Perm *p) { return bld_attractiveness(p); }
static float home_niceness_tp(const Perm *p) { return is_a(p, B_TELEPORTER) ? 100 : bld_attractiveness(p); }

#define private_tp_left (scratch->u.assign.tp_left)   /* per shard */

/* the best home among all permanents (whereMaxWithMax) */
static uint16_t best_home_anywhere(IsGoal goal, const void *ctx, Niceness nice, float stop_at) {
  uint16_t best = NONE;
  float bn = -1e30f;
  for (int i = 1; i < MAX_PERMS; i++) {
    Perm *p = &perms[i];
    if (!(p->flags & PF_USED) || !goal(p, ctx)) continue;
    float n = nice(p);
    if (n >= stop_at) return (uint16_t)i;
    if (n > bn) bn = n, best = (uint16_t)i;
  }
  return best;
}

/* HouseAssigner's search for a home for one citizen */
static void find_home(Cit *c) {
  if (c->home) return;
  uint16_t shard = cit_shard(c), newhome = NONE;
  if (shard < MAX_SHARDS && private_tp_left[shard] > 0) {
    if (c->job) {
      uint16_t js = P(c->job)->shard;
      newhome = finder_query(c->job, 0, 0, goal_home_shard_or_tp, &js, home_niceness_tp, -1, NULL, 99.999999f);
    } else {
      newhome = best_home_anywhere(goal_home_shard_or_tp, &shard, home_niceness_tp, 99.999999f);
    }
    if (newhome && is_a(P(newhome), B_TELEPORTER)) {
      newhome = NONE;
      for (int i = 1; i < MAX_PERMS && !newhome; i++) {
        Perm *p = &perms[i];
        if ((p->flags & PF_USED) && is_a(p, B_HOUSE) && p->shard == shard && bld_has_private_teleporter(p) && home_with_room(p))
          newhome = (uint16_t)i;
      }
    }
  } else if (c->job) {
    uint16_t js = P(c->job)->shard;
    newhome = finder_query(c->job, 0, 0, goal_home_shard, &js, home_niceness, -1, NULL, 99.999999f);
  } else {
    newhome = best_home_anywhere(goal_home_shard, &shard, home_niceness, 99.999999f);
  }
  if (newhome) {
    c->home = newhome;
    P(newhome)->nresidents++;
    if (bld_has_private_teleporter(P(newhome)) && shard < MAX_SHARDS) private_tp_left[shard]--;
  }
}

void assign_houses(void) {
  if (!(sim_flags & SIM_HOUSES_CHANGED)) return;
  static uint16_t people[101], places[101], transfer[101];
  memset(scratch->u.assign.tp_left, 0, sizeof scratch->u.assign.tp_left);
  uint16_t nshards = 0;
  for (int i = 1; i < MAX_PERMS; i++)
    if ((perms[i].flags & PF_USED) && perms[i].shard + 1 > nshards) nshards = (uint16_t)(perms[i].shard + 1);
  for (int w = 0; w < nworlds; w++)
    if (worlds[w].surfaceShard + 1 > nshards) nshards = (uint16_t)(worlds[w].surfaceShard + 1);
  /* each home's residents who may move (not forced to live there) */
  uint8_t *movable = scratch->u.assign.movable;
  memset(movable, 0, MAX_PERMS);
  for (int j = 1; j < MAX_CITS; j++)
    if ((cits[j].flags & CF_ALIVE) && cits[j].home && !(cits[j].flags & CF_FORCED_HOME)) movable[cits[j].home]++;
  for (uint16_t sh = 0; sh < nshards; sh++) {
    /* people in worse homes move to better ones with room */
    memset(people, 0, sizeof people);
    memset(places, 0, sizeof places);
    memset(transfer, 0, sizeof transfer);
    for (int i = 1; i < MAX_PERMS; i++) {
      Perm *p = &perms[i];
      if (!(p->flags & PF_USED) || p->shard != sh || !is_a(p, B_HOUSE)) continue;
      float a = bld_attractiveness(p);
      int q = (int)(a < 0 ? 0 : a > 100 ? 100 : a);
      if (is_a(p, B_WORK)) continue;   /* WorkWithHome */
      if (bld_has_private_teleporter(p) && remaining_home(p) > 0 && a > 99.9f && sh < MAX_SHARDS) private_tp_left[sh]++;
      people[q] = (uint16_t)(people[q] + movable[i]);
      places[q] = (uint16_t)(places[q] + remaining_home(p));
    }
    int cp = 0, cpl = 100;
    while (cp < cpl) {
      while (cp < cpl && people[cp] > 0) {
        int t = people[cp] < places[cpl] ? people[cp] : places[cpl];
        transfer[cp] = (uint16_t)(transfer[cp] + t);
        places[cpl] = (uint16_t)(places[cpl] - t);
        people[cp] = (uint16_t)(people[cp] - t);
        cpl--;
      }
      cp++;
    }
    for (int i = 1; i < MAX_PERMS; i++) {
      Perm *p = &perms[i];
      if (!(p->flags & PF_USED) || p->shard != sh || !is_a(p, B_HOUSE)) continue;
      float a = bld_attractiveness(p);
      int q = (int)(a < 0 ? 0 : a > 100 ? 100 : a);
      if (!transfer[q]) continue;
      for (int j = MAX_CITS - 1; j >= 1; j--) {
        Cit *c = &cits[j];
        if ((c->flags & CF_ALIVE) && c->home == i && transfer[q] > 0 && !(c->flags & CF_FORCED_HOME)) {
          cit_evict_from_home(c);
          transfer[q]--;
        }
      }
    }
  }
  uint8_t *impossible = scratch->u.assign.impossible;
  memset(impossible, 0, MAX_SHARDS);
  for (int j = 1; j < MAX_CITS; j++) {
    Cit *c = &cits[j];
    if (!(c->flags & CF_ALIVE)) continue;
    uint16_t sh = cit_shard(c);
    if (sh < MAX_SHARDS && impossible[sh]) continue;
    find_home(c);
    if (!c->home && sh < MAX_SHARDS) impossible[sh] = 1;
  }
  sim_flags &= (uint8_t)~SIM_HOUSES_CHANGED;
}

/* ---------------------------------------------------------------- jobs */
/* per class: workers wanted first (1000000: all of them); set by the worker distribution window. Every class: the
 * unique buildings come after the first 64 (the city's file keeps the rest after those, from version 2 on) */
int priority_jobs[CLS_COUNT];
static bool max_prio(int cls) { return cls < CLS_COUNT && priority_jobs[cls] == 1000000; }

void assign_jobs(void) {
  if (story_is("cityofthekey")) {
    int c = pcls_find("SecretSocietyHouse");
    if (c >= 0 && c < CLS_COUNT) priority_jobs[c] = 8;
  }
  if (!(sim_flags & SIM_JOBS_CHANGED)) return;
  int16_t *short_by = scratch->u.assign.short_by;
  memset(short_by, 0, sizeof scratch->u.assign.short_by);
  for (int k = 0; k < CLS_COUNT && k < npcls; k++) short_by[k] = max_prio(k) ? 0 : priority_jobs[k];
  uint16_t *with_job = scratch->u.assign.list;
  int nwj = 0;
  for (int i = 1; i < MAX_PERMS; i++) {
    Perm *p = &perms[i];
    if (!(p->flags & PF_USED) || !is_a(p, B_WORK)) continue;
    if (p->nworkers < bld_jobs(p)) with_job[nwj++] = (uint16_t)i;
    if (max_prio(p->cls)) short_by[p->cls] += bld_jobs(p);
    short_by[p->cls] -= p->nworkers;
  }
  bool prio_mode = false;
  for (int k = 0; k < npcls && k < 256; k++)
    if (short_by[k] > 0) prio_mode = true;
  bool anyone_without = false;
  for (int j = 1; j < MAX_CITS; j++)
    if ((cits[j].flags & CF_ALIVE) && (cits[j].flags & CF_WITHOUT_JOB)) { anyone_without = true; break; }
  if (!prio_mode && !anyone_without) {
    sim_flags &= (uint8_t)~SIM_JOBS_CHANGED;
    return;
  }
  bool cotk = story_is("cityofthekey");
  int secret = cotk ? pcls_find("SecretSocietyHouse") : -1;
  uint8_t *shard_no_options = scratch->u.assign.shard_no_options;
  memset(shard_no_options, 0, MAX_SHARDS);
  bool need_sort = true, any = true;
  while (any) {
    any = false;
    if (need_sort) {
      /* fewest workers first (a stable insertion sort, as the original's merge sort) */
      for (int i = 1; i < nwj; i++) {
        uint16_t k = with_job[i];
        int j = i - 1;
        while (j >= 0) {
          Perm *a = P(with_job[j]), *b = P(k);
          bool b_first = cotk && (int)b->cls == secret && (int)a->cls != secret;
          bool a_first = cotk && (int)a->cls == secret;
          if (!b_first && (a_first || a->nworkers <= b->nworkers)) break;
          with_job[j + 1] = with_job[j];
          j--;
        }
        with_job[j + 1] = k;
      }
      need_sort = false;
    }
    for (int bi = 0; bi < nwj; bi++) {
      Perm *b = P(with_job[bi]);
      if (b->nworkers >= bld_jobs(b)) continue;
      if (prio_mode && short_by[b->cls] <= 0) continue;
      Cit *best = NULL;
      for (int j = 1; j < MAX_CITS; j++) {
        Cit *c = &cits[j];
        if ((c->flags & CF_ALIVE) && (c->flags & CF_WITHOUT_JOB) && cit_shard(c) == b->shard) { best = c; break; }
      }
      if (prio_mode && !best) {
        if (b->shard < MAX_SHARDS && !shard_no_options[b->shard]) {
          Perm *fire = NULL;
          int fw = 0;
          for (int i = 1; i < MAX_PERMS; i++) {
            Perm *w = &perms[i];
            if (!(w->flags & PF_USED) || !is_a(w, B_WORK)) continue;
            if (w->shard == b->shard && w->nworkers > fw && short_by[w->cls] < 0) fire = w, fw = w->nworkers;
          }
          if (fire && fire->nworkers > 0) {
            /* the last of its workers */
            for (int j = MAX_CITS - 1; j >= 1 && !best; j--)
              if ((cits[j].flags & CF_ALIVE) && cits[j].job == pid(fire)) best = &cits[j];
            cit_lose_job(best, true);
            short_by[fire->cls]++;
          } else if (b->shard < MAX_SHARDS) {
            shard_no_options[b->shard] = 1;
          }
        }
      }
      if (best) {
        any = true;
        short_by[b->cls]--;
        best->flags &= (uint16_t)~CF_WITHOUT_JOB;
        best->job = pid(b);
        b->nworkers++;
        bld_after_give_job(b, best);
        if (cit_shard(best) == b->shard && (!best->home || !bld_keeps_residents(P(best->home)))) {
          cit_evict_from_home(best);
          bool assigned = false;
          if (is_a(b, B_HOUSE) && remaining_home(b) > 0) {
            best->home = pid(b);
            b->nresidents++;
            assigned = true;
          }
          if (!best->home || (!assigned && bld_has_private_teleporter(P(best->home)))) find_home(best);
        }
      }
    }
    if (!any && prio_mode) {
      any = true;
      prio_mode = false;
      need_sort = true;
    }
  }
  sim_flags &= (uint8_t)~SIM_JOBS_CHANGED;
}

/* ---------------------------------------------------------------- schools */
void assign_schools(void) {
  if (!(sim_flags & SIM_SCHOOLS_CHANGED)) return;
  uint16_t *schools = scratch->u.assign.list;
  int ns = 0;
  for (int i = 1; i < MAX_PERMS; i++) {
    Perm *p = &perms[i];
    if ((p->flags & PF_USED) && is_a(p, B_SCHOOL) && p->nstudents < bld_student_capacity(p) && p->nworkers > 0)
      schools[ns++] = (uint16_t)i;
  }
  /* fewest students first */
  for (int i = 1; i < ns; i++) {
    uint16_t k = schools[i];
    int j = i - 1;
    while (j >= 0 && P(schools[j])->nstudents > P(k)->nstudents) schools[j + 1] = schools[j], j--;
    schools[j + 1] = k;
  }
  bool any = true;
  int left = 0;
  for (int j = 1; j < MAX_CITS; j++)
    if ((cits[j].flags & CF_ALIVE) && cit_age(&cits[j]) < 16 && !cits[j].school) left++;
  while (left > 0 && any) {
    any = false;
    for (int i = ns - 1; i >= 0; i--) {
      Perm *s = P(schools[i]);
      Cit *st = NULL;
      for (int j = 1; j < MAX_CITS && !st; j++) {
        Cit *c = &cits[j];
        if (!(c->flags & CF_ALIVE) || cit_age(c) >= 16 || c->school) continue;
        if (cit_shard(c) == s->shard || (c->home && bld_has_private_teleporter(P(c->home)) && (worlds[s->world].flags & WF_HAS_TELEPORTER_ON_GROUP)))
          st = c;
      }
      if (st) {
        any = true;
        left--;
        st->school = pid(s);
        s->nstudents++;
        if (s->nstudents >= bld_student_capacity(s)) {
          for (int k = i; k + 1 < ns; k++) schools[k] = schools[k + 1];
          ns--;
        }
      }
      if (left == 0) break;
    }
  }
  sim_flags &= (uint8_t)~SIM_SCHOOLS_CHANGED;
}
