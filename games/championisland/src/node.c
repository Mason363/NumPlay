/* The display tree (see node.h). */
#include "node.h"
#include <math.h>
#include "spr.h"
#include "font.h"

/* on main's stack (node_set_memory): the calculator gives apps 32 KB of it apart from their RAM, and some calculator
 * software gives apps less RAM than 25.2's 153676 bytes */
#ifdef HOST
static Node nodes_mem[NODE_MAX];
Node *nodes = nodes_mem;
#else
Node *nodes;
#endif
void node_set_memory(Node *mem) { nodes = mem; }
static NodeId free_head;
static unsigned used_count;

/* scale and rotation set by code, for the few nodes that need them */
#define XF_MAX 48
typedef struct { NodeId n; float sx, sy, rot; } Xf;
static Xf xfs[XF_MAX];

/* texts set by code */
#define TX_MAX 48
typedef struct { NodeId n; int16_t lw; const char *s; int32_t color; } Tx;   /* lw: lineWidth set by code (0: as designed); color: RGB565, -1 as designed */
static Tx txs[TX_MAX];

/* event listeners */
#define EV_MAX 48
typedef struct { NodeId n; uint16_t ev; EventFn fn; void *ctx; } Listener;
static Listener evs[EV_MAX];

void node_reset(void) {
  memset(nodes, 0, NODE_MAX * sizeof *nodes);
  for (int i = 1; i < NODE_MAX - 1; i++) nodes[i].next = (NodeId)(i + 1);
  nodes[NODE_MAX - 1].next = 0;
  free_head = 1;
  used_count = 0;
  memset(xfs, 0, sizeof xfs);
  memset(txs, 0, sizeof txs);
  memset(evs, 0, sizeof evs);
}

unsigned node_count(void) { return used_count; }

#ifdef HOST
unsigned node_alloc_fails;   /* tests: nodes asked for with none left */
#endif
static NodeId alloc_node(void) {
  NodeId n = free_head;
#ifdef HOST
  if (!n) node_alloc_fails++;
#endif
  if (!n) return 0;
  free_head = nodes[n].next;
  memset(&nodes[n], 0, sizeof nodes[n]);
  nodes[n].flags = NF_USED | NF_VISIBLE | NF_ONSTAGE | NF_PLAYING | NF_TICK;
  nodes[n].flags2 = NF2_FRESH;
  nodes[n].alpha = 255;
  nodes[n].sym = NONE16;
  nodes[n].slot = NONE16;
  nodes[n].key = NONE16;
  nodes[n].name = NONE16;
  nodes[n].T = NONE16;
  nodes[n].ref = NONE16;
  used_count++;
  return n;
}

NodeId node_new(uint8_t kind) {
  NodeId n = alloc_node();
  if (n) nodes[n].kind = kind;
  return n;
}

/* ---------------------------------------------------------------- keys */
typedef struct {
  uint16_t start;
  uint8_t flags, kind;
  uint16_t ref;          /* symbol or payload */
  float x, y;
  int16_t rx4, ry4;
  uint16_t mat, name, sp;
  uint8_t alpha, mode, loop;
} Key;

static const uint8_t *read_varint(const uint8_t *p, unsigned *v) {
  unsigned r = 0, s = 0;
  for (;;) {
    uint8_t b = *p++;
    r |= (unsigned)(b & 0x7F) << s;
    if (!(b & 0x80)) break;
    s += 7;
  }
  *v = r;
  return p;
}

static const uint8_t *read_key(const uint8_t *p, Key *k) {
  unsigned start;
  p = read_varint(p, &start);
  k->start = (uint16_t)start;
  k->flags = *p++;
  if (k->flags & K_ABSENT) return p;
  if (k->flags & K_KIND) { k->kind = *p++; k->ref = rd16(p); p += 2; }
  else { k->kind = CK_SYM; k->ref = rd16(p); p += 2; }
  k->x = rds16(p) * 0.25f;
  k->y = rds16(p + 2) * 0.25f;
  p += 4;
  k->mat = 0;
  if (k->flags & K_MAT) { k->mat = rd16(p); p += 2; }
  k->rx4 = k->ry4 = 0;
  if (k->flags & K_REG) { k->rx4 = rds16(p); k->ry4 = rds16(p + 2); p += 4; }
  k->alpha = 255;
  if (k->flags & K_ALPHA) k->alpha = *p++;
  k->mode = MODE_INDEPENDENT; k->sp = 0; k->loop = 1;
  if (k->flags & K_CLIP) { k->mode = p[0]; k->sp = rd16(p + 1); k->loop = p[3]; p += 4; }
  k->name = NONE16;
  if (k->flags & K_NAME) { k->name = rd16(p); p += 2; }
  return p;
}

/* key of a slot at a frame: returns pointer after the slot; *idx = key index or NONE16 */
static const uint8_t *slot_key(const uint8_t *p, unsigned frame, Key *out, uint16_t *idx) {
  unsigned n;
  p = read_varint(p, &n);
  *idx = NONE16;
  for (unsigned i = 0; i < n; i++) {
    Key k;
    p = read_key(p, &k);
    if (k.start <= frame) { *out = k; *idx = (uint16_t)i; }
  }
  return p;
}

/* a clip that changes or that code can reach (static ones are drawn from the data) */
static bool sym_is_live_clip(uint16_t sym) {
  if (sym >= SYM_COUNT) return false;
  const uint8_t *p = ci_data + HDR(H_SYMS) + 8u * sym;
  return p[0] == SYM_CLIP && !(p[2] & SF_STATIC);
}

/* does this timeline child need a node? */
static bool needs_node(const Key *k) {
  if (k->name != NONE16) return true;
  if (k->kind == CK_TEXT) return true;
  if (k->kind == CK_SYM) return sym_is_live_clip(k->ref);
  return false;
}

static void apply_key(NodeId c, const Key *k, uint16_t idx) {
  Node *n = &nodes[c];
  n->key = idx;
  if (k->flags & K_ABSENT) { n->flags &= (uint8_t)~NF_ONSTAGE; return; }
  n->flags |= NF_ONSTAGE;
  n->x = k->x;
  n->y = k->y;
  n->rx4 = k->rx4;
  n->ry4 = k->ry4;
  n->mat = k->mat;
  n->alpha = k->alpha;
  /* a text or shape keyframe can change its look (a button's label turns white on focus) */
  if ((n->kind == NK_TEXT && k->kind == CK_TEXT) || (n->kind == NK_SHAPE && k->kind == CK_SHAPE)) n->ref = k->ref;
  if (k->flags & K_HIDDEN) n->flags &= (uint8_t)~NF_VISIBLE;
  else n->flags |= NF_VISIBLE;
  /* setting a transform from the timeline drops the code's */
  if (n->flags & NF_XFORM) {
    n->flags &= (uint8_t)~NF_XFORM;
    for (int i = 0; i < XF_MAX; i++) if (xfs[i].n == c) xfs[i].n = 0;
  }
}

static void instantiate_children(NodeId n);

NodeId node_new_sym(uint16_t sym) {
  SymInfo si;
  if (sym >= SYM_COUNT) return 0;
  sym_info(sym, &si);
  NodeId n = alloc_node();
  if (!n) return 0;
  Node *p = &nodes[n];
  p->sym = sym;
  switch (si.type) {
    case SYM_BITMAP: p->kind = NK_BITMAP; p->ref = (uint16_t)si.v; break;
    case SYM_SHAPE: p->kind = NK_SHAPE; p->ref = (uint16_t)si.v; break;
    case SYM_CLIP: {
      p->kind = NK_CLIP;
      Clip c;
      clip_get(sym, &c);
      p->T = c.T;
      if (c.T != NONE16) p->flags |= NF_ENTITY;
      instantiate_children(n);
      break;
    }
    default: p->kind = NK_CONT; break;
  }
  return n;
}

NodeId node_new_sym_lazy(uint16_t sym) {
  SymInfo si;
  if (sym >= SYM_COUNT) return 0;
  sym_info(sym, &si);
  if (si.type != SYM_CLIP) return node_new_sym(sym);
  NodeId n = alloc_node();
  if (!n) return 0;
  Clip c;
  clip_get(sym, &c);
  nodes[n].kind = NK_CLIP;
  nodes[n].sym = sym;
  nodes[n].T = c.T;
  if (c.T != NONE16) nodes[n].flags |= NF_ENTITY;
  nodes[n].flags2 |= NF2_LAZY;
  return n;
}

uint16_t node_lazy_sym = NONE16, node_dynamic_sym = NONE16;
static bool key_bounds(const Key *k, float *x, float *y, float *w, float *h);

static NodeId new_child_for(const Key *k) {
  NodeId c = 0;
  if (k->kind == CK_SYM)
    c = k->ref == node_lazy_sym ? node_new_sym_lazy(k->ref) : k->ref == node_dynamic_sym ? node_new_sym_dynamic(k->ref) : node_new_sym(k->ref);
  else if (k->kind == CK_SHAPE) { c = node_new(NK_SHAPE); if (c) nodes[c].ref = k->ref; }
  else if (k->kind == CK_TEXT) { c = node_new(NK_TEXT); if (c) nodes[c].ref = k->ref; }
  if (c) {
    nodes[c].name = k->name;
    nodes[c].flags2 = (uint8_t)((nodes[c].flags2 & ~(NF2_MODE | NF2_NOLOOP)) | (k->mode << 3) | (k->loop ? 0 : NF2_NOLOOP));
    if (k->mode != MODE_INDEPENDENT) nodes[c].frame = k->sp;
  }
  return c;
}

static bool only_current;   /* node_new_sym_frame: just what the frame shows */

NodeId node_new_sym_frame(uint16_t sym, int frame) {
  SymInfo si;
  if (sym >= SYM_COUNT) return 0;
  sym_info(sym, &si);
  if (si.type != SYM_CLIP) return node_new_sym(sym);
  NodeId n = alloc_node();
  if (!n) return 0;
  Clip c;
  clip_get(sym, &c);
  nodes[n].kind = NK_CLIP;
  nodes[n].sym = sym;
  nodes[n].T = c.T;
  if (c.T != NONE16) nodes[n].flags |= NF_ENTITY;
  nodes[n].frame = (uint16_t)(frame < c.nframes ? frame : 0);
  nodes[n].flags &= (uint8_t)~NF_PLAYING;
  only_current = true;
  instantiate_children(n);
  only_current = false;
  return n;
}

/* a long movie: only the children of its current frame, made and freed as it plays */
NodeId node_new_sym_dynamic(uint16_t sym) {
  NodeId n = node_new_sym_frame(sym, 0);
  if (n && nodes[n].kind == NK_CLIP) {
    nodes[n].flags2 |= NF2_DYN;
    nodes[n].flags |= NF_PLAYING;
  }
  return n;
}

/* Creates the nodes of the children a clip's timeline shows at any frame. */
static void instantiate_children(NodeId n) {
  Clip c;
  if (!clip_get(nodes[n].sym, &c)) return;
  if (only_current) nodes[n].flags2 |= NF2_PARTIAL;
  bool map_kids = c.T != NONE16 && comp_has(c.T, C_map);
  const uint8_t *p = c.slots;
  NodeId last = 0;
  for (unsigned s = 0; s < c.nslots; s++) {
    unsigned nk;
    const uint8_t *q = read_varint(p, &nk);
    Key first_present = {0}, cur = {0};
    bool has = false, has_cur = false;
    uint16_t cur_idx = NONE16;
    for (unsigned i = 0; i < nk; i++) {
      Key k;
      q = read_key(q, &k);
      if (!(k.flags & K_ABSENT) && !has) { first_present = k; has = true; }
      if (k.start <= nodes[n].frame) { cur = k; has_cur = true; cur_idx = (uint16_t)i; }
    }
    p = q;
    /* a map's children are all nodes: the map sorts them by depth */
    if (!has || !(needs_node(&first_present) || map_kids)) continue;
    if (only_current && (!has_cur || (cur.flags & K_ABSENT))) continue;
    if (only_current) first_present = cur;
    NodeId ch = new_child_for(&first_present);
    if (!ch) continue;
    nodes[ch].slot = (uint16_t)s;
    nodes[ch].parent = n;
    if (last) nodes[last].next = ch; else nodes[n].first = ch;
    last = ch;
    if (has_cur) apply_key(ch, &cur, cur_idx);
    else nodes[ch].flags &= (uint8_t)~NF_ONSTAGE;
  }
}

void (*node_free_hook)(NodeId n);
void (*node_stream_hook)(NodeId n);
void (*node_partial_hook)(NodeId n);
NodeId node_draw_hook_id;
void (*node_draw_hook)(NodeId n, Mat m, uint8_t alpha);

static void release(NodeId m) {
  if (nodes[m].ent && node_free_hook) node_free_hook(m);
  if (nodes[m].flags & NF_XFORM)
    for (int i = 0; i < XF_MAX; i++) if (xfs[i].n == m) xfs[i].n = 0;
  for (int i = 0; i < TX_MAX; i++) if (txs[i].n == m) txs[i].n = 0;
  for (int i = 0; i < EV_MAX; i++) if (evs[i].n == m) evs[i].n = 0;
  nodes[m].flags = 0;
  nodes[m].next = free_head;
  free_head = m;
  used_count--;
}

void node_free(NodeId n) {
  if (!n || !(nodes[n].flags & NF_USED)) return;
  node_remove(n);
  /* the subtree, deepest first, through its own links (a stack would have a
   * size: the climbing wall's map has some 130 children) */
  for (NodeId m = n;;) {
    while (nodes[m].first) m = nodes[m].first;
    NodeId p = nodes[m].parent;
    bool last = m == n;
    if (!last) nodes[p].first = nodes[m].next;   /* m was p's first child */
    release(m);
    if (last) break;
    m = p;
  }
}

#ifdef HOST
/* tests: a node in use that its parent does not list (0 if none) */
NodeId node_unlisted(void) {
  for (NodeId i = 1; i < NODE_MAX; i++) {
    if (!(nodes[i].flags & NF_USED) || !nodes[i].parent) continue;
    NodeId c = nodes[nodes[i].parent].first;
    int guard = 0;
    while (c && c != i && guard++ < NODE_MAX) c = nodes[c].next;
    if (c != i) return i;
  }
  return 0;
}
NodeId node_first_used(NodeId after) {   /* tests: the next node in use */
  for (NodeId i = (NodeId)(after + 1); i < NODE_MAX; i++)
    if (nodes[i].flags & NF_USED) return i;
  return 0;
}
#endif

void node_remove(NodeId c) {
  NodeId p = nodes[c].parent;
  if (!p) return;
  if (nodes[p].first == c) nodes[p].first = nodes[c].next;
  else
    for (NodeId s = nodes[p].first; s; s = nodes[s].next)
      if (nodes[s].next == c) { nodes[s].next = nodes[c].next; break; }
  nodes[c].parent = 0;
  nodes[c].next = 0;
}

void node_add_at(NodeId p, NodeId c, NodeId before) {
  if (!c) return;
  if (nodes[c].parent) node_remove(c);
  nodes[c].parent = p;
  if (nodes[p].first == before) { nodes[c].next = before; nodes[p].first = c; return; }
  for (NodeId s = nodes[p].first; s; s = nodes[s].next)
    if (nodes[s].next == before) { nodes[c].next = before; nodes[s].next = c; return; }
}

void node_add(NodeId p, NodeId c) {
  node_add_at(p, c, 0);
  nodes[c].slot = NONE16;
  nodes[c].flags |= NF_ONSTAGE;
}

/* names are "name", "property" or "name|property" (see tools/pack.py) */
static bool name_is(uint16_t id, const char *name) {
  if (id == NONE16) return false;
  const char *s = str(id);
  size_t n = strlen(name);
  while (*s) {
    const char *e = strchr(s, '|');
    size_t l = e ? (size_t)(e - s) : strlen(s);
    if (l == n && !memcmp(s, name, n)) return true;
    if (!e) break;
    s = e + 1;
  }
  return false;
}

NodeId node_child(NodeId n, const char *name) {
  for (NodeId c = nodes[n].first; c; c = nodes[c].next)
    if (name_is(nodes[c].name, name)) return c;
  return 0;
}

NodeId node_find(NodeId n, const char *name) {
  for (NodeId c = nodes[n].first; c; c = nodes[c].next) {
    if (name_is(nodes[c].name, name)) return c;
    NodeId r = node_find(c, name);
    if (r) return r;
  }
  return 0;
}

bool node_named(NodeId n, const char *name) { return name_is(nodes[n].name, name); }

int node_index(NodeId p, NodeId c) {
  int i = 0;
  for (NodeId s = nodes[p].first; s; s = nodes[s].next, i++)
    if (s == c) return i;
  return -1;
}

NodeId node_nth(NodeId p, int i) {
  NodeId s = nodes[p].first;
  while (s && i-- > 0) s = nodes[s].next;
  return s;
}

/* ---------------------------------------------------------------- timeline */
int node_frames(NodeId n) {
  Clip c;
  return nodes[n].kind == NK_CLIP && clip_get(nodes[n].sym, &c) ? c.nframes : 1;
}

void node_play(NodeId n) { nodes[n].flags |= NF_PLAYING; }
void node_stop(NodeId n) { nodes[n].flags &= (uint8_t)~NF_PLAYING; }

static void run_actions(NodeId n, unsigned frame) {
  Clip c;
  if (!clip_get(nodes[n].sym, &c)) return;
  for (int i = 0; i < c.nacts; i++) {
    const uint8_t *a = c.acts + 5 * i;
    if (rd16(a) != frame) continue;
    switch (a[2]) {
      case ACT_STOP: nodes[n].flags &= (uint8_t)~NF_PLAYING; break;
      case ACT_PLAY: nodes[n].flags |= NF_PLAYING; break;
      case ACT_DISPATCH: node_emit(n, rd16(a + 3)); break;
      case ACT_DISPATCH_PARENT: if (nodes[n].parent) node_emit(nodes[n].parent, rd16(a + 3)); break;
    }
  }
}

bool node_goto(NodeId n, const char *label, int frame, bool play) {
  Clip c;
  if (nodes[n].kind != NK_CLIP || !clip_get(nodes[n].sym, &c)) return false;
  if (label) {
    frame = clip_label(&c, label);
    if (frame < 0) return false;
  }
  if (frame >= c.nframes) frame = c.nframes - 1;
  if (frame < 0) frame = 0;
  nodes[n].frame = (uint16_t)frame;
  nodes[n].flags2 &= (uint8_t)~NF2_FRESH;
  if (play) nodes[n].flags |= NF_PLAYING; else nodes[n].flags &= (uint8_t)~NF_PLAYING;
  run_actions(n, (unsigned)frame);
  return true;
}

bool node_has_label(NodeId n, const char *label) {
  Clip c;
  return nodes[n].kind == NK_CLIP && clip_get(nodes[n].sym, &c) && clip_label(&c, label) >= 0;
}

const char *node_label(NodeId n) {
  Clip c;
  if (nodes[n].kind != NK_CLIP || !clip_get(nodes[n].sym, &c)) return "";
  const char *best = "";
  for (int i = 0; i < c.nlabels; i++)
    if (rd16(c.labels + 4 * i) <= nodes[n].frame) best = str(rd16(c.labels + 4 * i + 2));
  return best;
}

/* The Ticker: every playing independent clip moves on one frame. */
static void tick_rec(NodeId n) {
  Node *p = &nodes[n];
  /* CreateJS ticks what is on the display list: a timeline child that is not
   * on stage at its parent's frame is out of it */
  if (!(p->flags & NF_TICK) || !(p->flags & NF_ONSTAGE)) return;
  if (p->flags2 & NF2_FRESH) {
    /* a new clip's first tick puts it at its frame and runs that frame's script (a stop there holds it) */
    p->flags2 &= (uint8_t)~NF2_FRESH;
    if (p->kind == NK_CLIP && node_mode(p) == MODE_INDEPENDENT) {
      run_actions(n, p->frame);
      for (NodeId ch = nodes[n].first; ch; ch = nodes[ch].next) tick_rec(ch);
      return;
    }
  }
  if (p->kind == NK_CLIP && node_mode(p) == MODE_INDEPENDENT && (p->flags & NF_PLAYING)) {
    Clip c;
    if (clip_get(p->sym, &c) && c.nframes > 1) {
      unsigned f = p->frame + 1u;
      if (f >= c.nframes) f = node_loops(p) ? 0 : c.nframes - 1u;
      if (f != p->frame) {
        p->frame = (uint16_t)f;
        run_actions(n, f);
      }
    }
  }
  for (NodeId ch = nodes[n].first; ch; ch = nodes[ch].next) tick_rec(ch);
}

void node_tick(NodeId root) { tick_rec(root); }

/* Timelines put their children where they belong at the current frame. */
static void update_rec(NodeId n) {
  Node *p = &nodes[n];
  if (p->kind == NK_CLIP && p->sym != NONE16) {
    Clip c;
    if (clip_get(p->sym, &c) && c.nframes > 1) {
      const uint8_t *q = c.slots;
      NodeId ch = p->first, prev = 0;
      bool dyn = p->flags2 & NF2_DYN, partial = p->flags2 & NF2_PARTIAL;
      for (unsigned s = 0; s < c.nslots; s++) {
        while (ch && nodes[ch].slot != NONE16 && nodes[ch].slot < s) { prev = ch; ch = nodes[ch].next; }
        if (dyn || partial) {
          /* a long movie: its children exist while they are on stage; a
           * partial clip: from when they are first on stage */
          Key k;
          uint16_t idx;
          const uint8_t *q2 = slot_key(q, p->frame, &k, &idx);
          bool present = idx != NONE16 && !(k.flags & K_ABSENT);
          bool have = ch && nodes[ch].slot == s;
          if (present && !have && needs_node(&k)) {
            bool oc = only_current;
            only_current = partial && !dyn;     /* its own children: made when shown too */
            NodeId nc = new_child_for(&k);
            only_current = oc;
            if (nc) {
              nodes[nc].slot = (uint16_t)s;
              nodes[nc].parent = n;
              nodes[nc].next = ch;
              nodes[nc].key = NONE16;           /* its key is applied just below */
              nodes[nc].flags &= (uint8_t)~NF_ONSTAGE;
              if (prev) nodes[prev].next = nc; else nodes[n].first = nc;
              ch = nc;
              if (dyn && node_stream_hook) node_stream_hook(nc);
              if (!dyn && node_partial_hook) node_partial_hook(nc);
            }
          } else if (dyn && !present && have && !(nodes[ch].flags2 & NF2_KEEP) && nodes[ch].name == NONE16) {
            NodeId nx = nodes[ch].next;
            node_free(ch);
            ch = nx;
            q = q2;
            continue;
          }
        }
        if (ch && nodes[ch].slot == s) {
          Key k;
          uint16_t idx;
          q = slot_key(q, p->frame, &k, &idx);
          if (idx == NONE16) nodes[ch].flags &= (uint8_t)~NF_ONSTAGE;
          else if (idx != nodes[ch].key) {
            bool was = nodes[ch].flags & NF_ONSTAGE;
            apply_key(ch, &k, idx);
            /* a synched child follows its parent's frame */
            if (!was && node_mode(&nodes[ch]) == MODE_INDEPENDENT && nodes[ch].kind == NK_CLIP) {
              nodes[ch].frame = 0;   /* re-entering the stage restarts it, as Animate does */
              nodes[ch].flags |= NF_PLAYING;
              nodes[ch].flags2 |= NF2_FRESH;
            }
          }
          if (node_mode(&nodes[ch]) == MODE_SYNCHED && idx != NONE16) {
            int f = (int)k.sp + (int)p->frame - (int)k.start;
            int nf = node_frames(ch);
            if (node_loops(&nodes[ch]) && nf > 0) f %= nf;
            else if (f >= nf) f = nf - 1;
            nodes[ch].frame = (uint16_t)f;
          } else if (node_mode(&nodes[ch]) == MODE_SINGLE && idx != NONE16)
            nodes[ch].frame = k.sp;
        } else {
          unsigned nk;
          const uint8_t *r = read_varint(q, &nk);
          for (unsigned i = 0; i < nk; i++) { Key k; r = read_key(r, &k); }
          q = r;
        }
      }
    }
  }
  for (NodeId ch = p->first; ch; ch = nodes[ch].next) update_rec(ch);
}

void node_update(NodeId root) { update_rec(root); }

/* just this clip's own timeline (not its descendants) */
void node_update_one(NodeId n) {
  NodeId saved = nodes[n].first;
  (void)saved;
  Node *p = &nodes[n];
  if (p->kind != NK_CLIP || p->sym == NONE16) return;
  Clip c;
  if (!clip_get(p->sym, &c)) return;
  const uint8_t *q = c.slots;
  NodeId ch = p->first;
  for (unsigned s = 0; s < c.nslots; s++) {
    while (ch && nodes[ch].slot != NONE16 && nodes[ch].slot < s) ch = nodes[ch].next;
    Key k;
    uint16_t idx;
    q = slot_key(q, p->frame, &k, &idx);
    if (!(ch && nodes[ch].slot == s)) continue;
    if (idx == NONE16) nodes[ch].flags &= (uint8_t)~NF_ONSTAGE;
    else if (idx != nodes[ch].key) {
      bool was = nodes[ch].flags & NF_ONSTAGE;
      apply_key(ch, &k, idx);
      if (!was && node_mode(&nodes[ch]) == MODE_INDEPENDENT && nodes[ch].kind == NK_CLIP) {
        nodes[ch].frame = 0;
        nodes[ch].flags |= NF_PLAYING;
      }
    }
  }
}

NodeId node_onstage_child(NodeId n) {
  for (NodeId c = nodes[n].first; c; c = nodes[c].next)
    if (nodes[c].flags & NF_ONSTAGE) return c;
  return 0;
}

/* ---------------------------------------------------------------- transforms */
static Xf *xf_of(NodeId n, bool make) {
  for (int i = 0; i < XF_MAX; i++) if (xfs[i].n == n) return &xfs[i];
  if (!make) return NULL;
  for (int i = 0; i < XF_MAX; i++)
    if (!xfs[i].n) { xfs[i].n = n; return &xfs[i]; }
  return NULL;
}

void node_xform(NodeId n, float sx, float sy, float rot) {
  Xf *x = xf_of(n, true);
  if (!x) return;
  x->sx = sx; x->sy = sy; x->rot = rot;
  nodes[n].flags |= NF_XFORM;
}

void node_get_xform(NodeId n, float *sx, float *sy, float *rot) {
  if (nodes[n].flags & NF_XFORM) {
    Xf *x = xf_of(n, false);
    if (x) { *sx = x->sx; *sy = x->sy; *rot = x->rot; return; }
  }
  const float *m = mat(nodes[n].mat);
  *sx = sqrtf(m[0] * m[0] + m[1] * m[1]);
  *sy = sqrtf(m[2] * m[2] + m[3] * m[3]);
  if (m[0] * m[3] - m[1] * m[2] < 0) *sx = -*sx;
  *rot = atan2f(m[1], m[0]) * (180.0f / 3.14159265f);
  if (*sx < 0) *rot = 0;
}

Mat node_local(NodeId id) {
  const Node *n = &nodes[id];
  float a, b, c, d;
  if (n->flags & NF_XFORM) {
    Xf *x = xf_of(id, false);
    float r = x ? x->rot * (3.14159265f / 180.0f) : 0, sx = x ? x->sx : 1, sy = x ? x->sy : 1;
    float cs = r ? cosf(r) : 1, sn = r ? sinf(r) : 0;
    a = cs * sx; b = sn * sx; c = -sn * sy; d = cs * sy;
  } else {
    const float *m = mat(n->mat);
    a = m[0]; b = m[1]; c = m[2]; d = m[3];
  }
  float rx = n->rx4 * 0.25f, ry = n->ry4 * 0.25f;
  return (Mat){a, b, c, d, n->x - (rx * a + ry * c), n->y - (rx * b + ry * d)};
}

Mat node_to(NodeId n, NodeId anc) {
  Mat m = MAT_ID;
  for (NodeId p = n; p && p != anc; p = nodes[p].parent) m = mat_mul(node_local(p), m);
  return m;
}

Mat node_global(NodeId n) { return node_to(n, 0); }

bool node_visible_chain(NodeId n) {
  for (; n; n = nodes[n].parent)
    if (!node_visible(n)) return false;
  return true;
}

void node_set_visible(NodeId n, bool v) {
  if (v) nodes[n].flags |= NF_VISIBLE; else nodes[n].flags &= (uint8_t)~NF_VISIBLE;
}

/* ---------------------------------------------------------------- bounds and hits */
static bool shape_bounds(const uint8_t *sh, float *x, float *y, float *w, float *h) {
  float lx = 1e9f, ly = 1e9f, hx = -1e9f, hy = -1e9f;
  const uint8_t *p = sh + 1;
  for (int s = 0; s < sh[0]; s++) {
    int np = p[4];
    p += 5;
    for (int k = 0; k < np; k++) {
      int n = rd16(p);
      p += 2;
      for (int i = 0; i < n; i++, p += 4) {
        float px = rds16(p) * 0.25f, py = rds16(p + 2) * 0.25f;
        lx = px < lx ? px : lx;
        hx = px > hx ? px : hx;
        ly = py < ly ? py : ly;
        hy = py > hy ? py : hy;
      }
    }
  }
  if (lx > hx) return false;
  *x = lx; *y = ly; *w = hx - lx; *h = hy - ly;
  return true;
}

/* CreateJS getBounds: a bitmap's rectangle, a shape's extent, and for a
 * clip the union of its visible children's bounds at its current frame */
typedef struct { float lx, ly, hx, hy; bool any, texts; } Acc;   /* texts: count them too */

static void acc_rect(Acc *a, Mat m, float x, float y, float w, float h) {
  float xs[4] = {x, x + w, x, x + w}, ys[4] = {y, y, y + h, y + h};
  for (int i = 0; i < 4; i++) {
    float X = m.a * xs[i] + m.c * ys[i] + m.tx, Y = m.b * xs[i] + m.d * ys[i] + m.ty;
    if (!a->any) { a->lx = a->hx = X; a->ly = a->hy = Y; a->any = true; continue; }
    a->lx = X < a->lx ? X : a->lx; a->hx = X > a->hx ? X : a->hx;
    a->ly = Y < a->ly ? Y : a->ly; a->hy = Y > a->hy ? Y : a->hy;
  }
}

static Mat key_mat(const Key *k) {
  float a = 1, b = 0, c = 0, d = 1;
  if (k->mat) { const float *mm = mat(k->mat); a = mm[0]; b = mm[1]; c = mm[2]; d = mm[3]; }
  float rx = k->rx4 * 0.25f, ry = k->ry4 * 0.25f;
  return (Mat){a, b, c, d, k->x - (rx * a + ry * c), k->y - (rx * b + ry * d)};
}

/* a text's box as CreateJS measures it: its lines in the font at its size
 * (the font is 10 px), from its anchor (alignment, baseline) */
static void text_acc(const uint8_t *t, Mat m, Acc *a) {
  float k = t[2] * 0.1f;
  int lw = rds16(t + 8) / 4, w, h;
  if (!k) return;
  font_measure(str(rd16(t)), lw > 0 ? (int)(lw / k) : 0, 0, &w, &h);
  float fw = w * k, fh = h * k;
  acc_rect(a, m, t[6] == 1 ? -fw / 2 : t[6] == 2 ? -fw : 0, -(t[7] == 1 ? 6 : t[7] >= 2 ? 10 : 1) * k, fw, fh);
}

static void sym_acc(uint16_t sym, unsigned frame, Mat m, Acc *a, int depth) {
  if (sym >= SYM_COUNT || depth > 12) return;
  SymInfo si;
  sym_info(sym, &si);
  float x, y, w, h;
  if (si.type == SYM_BITMAP) {
    Sprite sp;
    sprite_info((uint16_t)si.v, &sp);
    acc_rect(a, m, 0, 0, sp.w, sp.h);
  } else if (si.type == SYM_SHAPE) {
    if (shape_bounds(payload((uint16_t)si.v), &x, &y, &w, &h)) acc_rect(a, m, x, y, w, h);
  } else if (si.type == SYM_CLIP) {
    Clip c;
    if (!clip_get(sym, &c)) return;
    if (c.nframes && frame >= c.nframes) frame %= c.nframes;
    const uint8_t *q = c.slots;
    for (unsigned s = 0; s < c.nslots; s++) {
      Key k;
      uint16_t idx;
      q = slot_key(q, frame, &k, &idx);
      if (idx == NONE16 || (k.flags & (K_ABSENT | K_HIDDEN))) continue;
      Mat km = mat_mul(m, key_mat(&k));
      if (k.kind == CK_SHAPE) { if (shape_bounds(payload(k.ref), &x, &y, &w, &h)) acc_rect(a, km, x, y, w, h); }
      else if (k.kind == CK_TEXT) { if (a->texts) text_acc(payload(k.ref), km, a); }
      else if (k.kind == CK_SYM) {
        unsigned f = k.mode == MODE_SYNCHED ? k.sp + frame - k.start : k.mode == MODE_SINGLE ? k.sp : 0;
        sym_acc(k.ref, f, km, a, depth + 1);
      }
    }
  }
}

static void node_acc(NodeId id, Mat m, Acc *a, int depth) {
  const Node *n = &nodes[id];
  float x, y, w, h;
  if (depth > 12) return;
  if (n->kind == NK_BITMAP) {
    Sprite sp;
    sprite_info(n->ref, &sp);
    acc_rect(a, m, 0, 0, sp.w, sp.h);
    return;
  }
  if (n->kind == NK_SHAPE) {
    if (n->ref != NONE16 && shape_bounds(payload(n->ref), &x, &y, &w, &h)) acc_rect(a, m, x, y, w, h);
    return;
  }
  if (n->kind == NK_TEXT) return;
  Clip c;
  NodeId ch = n->first;
  if (n->kind == NK_CLIP && !(n->flags & NF_SORTED) && clip_get(n->sym, &c)) {
    const uint8_t *q = c.slots;
    for (unsigned s = 0; s < c.nslots; s++) {
      while (ch && nodes[ch].slot != NONE16 && nodes[ch].slot < s) ch = nodes[ch].next;
      Key k;
      uint16_t idx;
      q = slot_key(q, n->frame, &k, &idx);
      if (ch && nodes[ch].slot == s) {
        if (node_visible(ch)) node_acc(ch, mat_mul(m, node_local(ch)), a, depth + 1);
        continue;
      }
      if (idx == NONE16 || (k.flags & (K_ABSENT | K_HIDDEN)) || needs_node(&k)) continue;
      Mat km = mat_mul(m, key_mat(&k));
      if (k.kind == CK_SHAPE) { if (shape_bounds(payload(k.ref), &x, &y, &w, &h)) acc_rect(a, km, x, y, w, h); }
      else if (k.kind == CK_SYM) {
        unsigned f = k.mode == MODE_SYNCHED ? k.sp + n->frame - k.start : k.mode == MODE_SINGLE ? k.sp : 0;
        sym_acc(k.ref, f, km, a, depth + 1);
      }
    }
    for (NodeId x2 = n->first; x2; x2 = nodes[x2].next)
      if (nodes[x2].slot == NONE16 && node_visible(x2)) node_acc(x2, mat_mul(m, node_local(x2)), a, depth + 1);
    return;
  }
  for (NodeId x2 = n->first; x2; x2 = nodes[x2].next)
    if (node_visible(x2)) node_acc(x2, mat_mul(m, node_local(x2)), a, depth + 1);
}

bool node_bounds(NodeId n, float *x, float *y, float *w, float *h) {
  Acc a = {0, 0, 0, 0, false, false};
  node_acc(n, MAT_ID, &a, 0);
  if (!a.any) {
    /* nothing to show: Animate's nominal bounds, if any */
    Clip c;
    if (nodes[n].kind == NK_CLIP && clip_get(nodes[n].sym, &c) && (c.nb[2] || c.nb[3])) {
      *x = c.nb[0]; *y = c.nb[1]; *w = c.nb[2]; *h = c.nb[3];
      return true;
    }
    return false;
  }
  *x = a.lx; *y = a.ly; *w = a.hx - a.lx; *h = a.hy - a.ly;
  return true;
}

bool node_bounds_in(NodeId n, NodeId space, float *x, float *y, float *w, float *h) {
  float bx, by, bw, bh;
  if (!node_bounds(n, &bx, &by, &bw, &bh)) return false;
  Mat m = node_to(n, space);
  float xs[4] = {bx, bx + bw, bx, bx + bw}, ys[4] = {by, by, by + bh, by + bh};
  float lx = 1e9f, ly = 1e9f, hx = -1e9f, hy = -1e9f;
  for (int i = 0; i < 4; i++) {
    float X = m.a * xs[i] + m.c * ys[i] + m.tx, Y = m.b * xs[i] + m.d * ys[i] + m.ty;
    lx = X < lx ? X : lx;
    hx = X > hx ? X : hx;
    ly = Y < ly ? Y : ly;
    hy = Y > hy ? Y : hy;
  }
  *x = lx; *y = ly; *w = hx - lx; *h = hy - ly;
  return true;
}

static bool point_in_shape(const uint8_t *sh, float u, float v) {
  const uint8_t *p = sh + 1;
  bool any = false;
  for (int s = 0; s < sh[0]; s++) {
    int np = p[4];
    p += 5;
    bool in = false;
    for (int k = 0; k < np; k++) {
      int n = rd16(p);
      const uint8_t *q = p + 2;
      for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = rds16(q + 4 * i) * 0.25f, yi = rds16(q + 4 * i + 2) * 0.25f;
        float xj = rds16(q + 4 * j) * 0.25f, yj = rds16(q + 4 * j + 2) * 0.25f;
        if ((yi > v) != (yj > v) && u < (xj - xi) * (v - yi) / (yj - yi) + xi) in = !in;
      }
      p += 2 + 4 * n;
    }
    any |= in;
  }
  return any;
}

/* is local point (u, v) of node n on something it draws (shapes and bitmaps)? */
static bool hit_rec(NodeId n, float u, float v) {
  const Node *p = &nodes[n];
  if (p->kind == NK_SHAPE && p->ref != NONE16) return point_in_shape(payload(p->ref), u, v);
  if (p->kind == NK_BITMAP) {
    Sprite s;
    sprite_info(p->ref, &s);
    return u >= 0 && v >= 0 && u < s.w && v < s.h;
  }
  for (NodeId c = p->first; c; c = nodes[c].next) {
    if (!(nodes[c].flags & NF_ONSTAGE)) continue;
    Mat m = node_local(c);
    float det = m.a * m.d - m.b * m.c;
    if (fabsf(det) < 1e-9f) continue;
    float dx = u - m.tx, dy = v - m.ty;
    float lu = (m.d * dx - m.c * dy) / det, lv = (-m.b * dx + m.a * dy) / det;
    if (hit_rec(c, lu, lv)) return true;
  }
  /* virtual children: shapes and bitmaps of the timeline */
  Clip c;
  if (p->kind == NK_CLIP && clip_get(p->sym, &c)) {
    const uint8_t *q = c.slots;
    for (unsigned s = 0; s < c.nslots; s++) {
      Key k;
      uint16_t idx;
      q = slot_key(q, p->frame, &k, &idx);
      if (idx == NONE16 || (k.flags & K_ABSENT) || needs_node(&k)) continue;
      float a = 1, b = 0, cc = 0, d = 1;
      if (k.mat) { const float *mm = mat(k.mat); a = mm[0]; b = mm[1]; cc = mm[2]; d = mm[3]; }
      float tx = k.x - (k.rx4 * 0.25f * a + k.ry4 * 0.25f * cc), ty = k.y - (k.rx4 * 0.25f * b + k.ry4 * 0.25f * d);
      float det = a * d - b * cc;
      if (fabsf(det) < 1e-9f) continue;
      float dx = u - tx, dy = v - ty;
      float lu = (d * dx - cc * dy) / det, lv = (-b * dx + a * dy) / det;
      if (k.kind == CK_SHAPE) { if (point_in_shape(payload(k.ref), lu, lv)) return true; }
      else if (k.kind == CK_SYM) {
        SymInfo si;
        sym_info(k.ref, &si);
        if (si.type == SYM_SHAPE && point_in_shape(payload((uint16_t)si.v), lu, lv)) return true;
        if (si.type == SYM_BITMAP) {
          Sprite sp;
          sprite_info((uint16_t)si.v, &sp);
          if (lu >= 0 && lv >= 0 && lu < sp.w && lv < sp.h) return true;
        }
      }
    }
  }
  return false;
}

bool node_hit(NodeId n, float u, float v) { return hit_rec(n, u, v); }

/* ---------------------------------------------------------------- streaming */
static bool key_bounds(const Key *k, float *x, float *y, float *w, float *h) {
  float bx = 0, by = 0, bw = 0, bh = 0;
  if (k->kind != CK_SYM || k->ref >= SYM_COUNT) return false;
  SymInfo si;
  sym_info(k->ref, &si);
  if (si.type == SYM_CLIP) {
    Clip c;
    clip_get(k->ref, &c);
    bx = c.nb[0]; by = c.nb[1]; bw = c.nb[2]; bh = c.nb[3];
  } else if (si.type == SYM_BITMAP) {
    Sprite sp;
    sprite_info((uint16_t)si.v, &sp);
    bw = sp.w; bh = sp.h;
  } else return false;
  float a = 1, b = 0, c = 0, d = 1;
  if (k->mat) { const float *m = mat(k->mat); a = m[0]; b = m[1]; c = m[2]; d = m[3]; }
  float tx = k->x - (k->rx4 * 0.25f * a + k->ry4 * 0.25f * c), ty = k->y - (k->rx4 * 0.25f * b + k->ry4 * 0.25f * d);
  float xs[4] = {bx, bx + bw, bx, bx + bw}, ys[4] = {by, by, by + bh, by + bh};
  float lx = 1e9f, ly = 1e9f, hx = -1e9f, hy = -1e9f;
  for (int i = 0; i < 4; i++) {
    float X = a * xs[i] + c * ys[i] + tx, Y = b * xs[i] + d * ys[i] + ty;
    lx = X < lx ? X : lx; hx = X > hx ? X : hx; ly = Y < ly ? Y : ly; hy = Y > hy ? Y : hy;
  }
  *x = lx; *y = ly; *w = hx - lx; *h = hy - ly;
  return true;
}

bool node_slot_bounds(NodeId n, unsigned slot, float *x, float *y, float *w, float *h) {
  Clip c;
  if (!clip_get(nodes[n].sym, &c) || slot >= c.nslots) return false;
  const uint8_t *q = c.slots;
  for (unsigned s = 0; s < c.nslots; s++) {
    Key k;
    uint16_t idx;
    q = slot_key(q, nodes[n].frame, &k, &idx);
    if (s == slot) return idx != NONE16 && !(k.flags & K_ABSENT) && key_bounds(&k, x, y, w, h);
  }
  return false;
}

void clip_each_slot(uint16_t sym, unsigned frame, bool (*fn)(unsigned slot, const SlotInfo *si, void *ctx), void *ctx) {
  Clip c;
  if (!clip_get(sym, &c)) return;
  const uint8_t *q = c.slots;
  for (unsigned s = 0; s < c.nslots; s++) {
    Key k;
    uint16_t idx;
    q = slot_key(q, frame, &k, &idx);
    if (idx == NONE16 || (k.flags & K_ABSENT) || k.kind != CK_SYM) continue;
    SlotInfo si = {k.ref, k.name, 0, 0, 0, 0, 0, 0, false};
    float a = 1, b = 0, cc = 0, d = 1;
    if (k.mat) { const float *m = mat(k.mat); a = m[0]; b = m[1]; cc = m[2]; d = m[3]; }
    si.x = k.x - (k.rx4 * 0.25f * a + k.ry4 * 0.25f * cc);
    si.y = k.y - (k.rx4 * 0.25f * b + k.ry4 * 0.25f * d);
    si.has_bounds = key_bounds(&k, &si.bx, &si.by, &si.bw, &si.bh);
    if (!fn(s, &si, ctx)) return;
  }
}

/* where a timeline child shows (in the clip's space): a clip's nominal
 * bounds are not in the data (the doodle's compiler renamed nominalBounds),
 * so its bounds are what its first frame shows (CreateJS getBounds) */
static bool child_bounds(const Key *k, float *x, float *y, float *w, float *h) {
  if (!key_bounds(k, x, y, w, h)) return false;
  if (*w || *h) return true;
  Acc a = {0, 0, 0, 0, false, true};
  sym_acc(k->ref, k->mode != MODE_INDEPENDENT ? k->sp : 0, key_mat(k), &a, 0);
  if (a.any) { *x = a.lx; *y = a.ly; *w = a.hx - a.lx; *h = a.hy - a.ly; }
  return true;
}

/* how far a lazy clip's children reach out of their origins (left, up,
 * right, down), measured once: only those whose origin is that near the
 * rectangle need measuring */
static uint16_t reach_sym = NONE16, reach_frame;
static float reach[4];
static void measure_reach(NodeId n, const Clip *c) {
  reach_sym = nodes[n].sym;
  reach_frame = nodes[n].frame;
  memset(reach, 0, sizeof reach);
  const uint8_t *q = c->slots;
  for (unsigned s = 0; s < c->nslots; s++) {
    Key k;
    uint16_t idx;
    float bx, by, bw, bh;
    q = slot_key(q, nodes[n].frame, &k, &idx);
    if (idx == NONE16 || (k.flags & K_ABSENT) || !child_bounds(&k, &bx, &by, &bw, &bh)) continue;
    reach[0] = fmaxf(reach[0], k.x - bx);
    reach[1] = fmaxf(reach[1], k.y - by);
    reach[2] = fmaxf(reach[2], bx + bw - k.x);
    reach[3] = fmaxf(reach[3], by + bh - k.y);
  }
}

void node_stream(NodeId n, float x0, float y0, float x1, float y1) {
  Clip c;
  if (!(nodes[n].flags2 & NF2_LAZY) || !clip_get(nodes[n].sym, &c)) return;
  /* the timeline children it has, by slot (in any order: a map sorts them) */
  enum { HAVE_MAX = 192 };
  NodeId have[HAVE_MAX];
  int nh = 0;
  for (NodeId ch = nodes[n].first; ch; ch = nodes[ch].next) {
    if (nodes[ch].slot == NONE16 || nh >= HAVE_MAX) continue;
    int j = nh++;
    while (j > 0 && nodes[have[j - 1]].slot > nodes[ch].slot) { have[j] = have[j - 1]; j--; }
    have[j] = ch;
  }
  const uint8_t *q = c.slots;
  int hi = 0;
  bool map_kids = c.T != NONE16 && comp_has(c.T, C_map);
  for (unsigned s = 0; s < c.nslots; s++) {
    Key k;
    uint16_t idx;
    q = slot_key(q, nodes[n].frame, &k, &idx);
    while (hi < nh && nodes[have[hi]].slot < s) hi++;
    NodeId got = hi < nh && nodes[have[hi]].slot == s ? have[hi] : 0;
    bool present = idx != NONE16 && !(k.flags & K_ABSENT);
    /* near: its box (a clip's: its origin) in the rectangle, else what it shows */
    float bx, by, bw, bh;
    bool near = false;
    if (present && (needs_node(&k) || map_kids) && key_bounds(&k, &bx, &by, &bw, &bh)) {
      near = bx < x1 && by < y1 && bx + bw > x0 && by + bh > y0;
      if (!near && (reach_sym != nodes[n].sym || reach_frame != nodes[n].frame)) measure_reach(n, &c);
      if (!near && k.x - reach[0] < x1 && k.y - reach[1] < y1 && k.x + reach[2] > x0 && k.y + reach[3] > y0)
        near = child_bounds(&k, &bx, &by, &bw, &bh) && bx < x1 && by < y1 && bx + bw > x0 && by + bh > y0;
    }
    if (near && !got) {
      if (nh >= HAVE_MAX) continue;
      NodeId nc = new_child_for(&k);
      if (!nc) continue;
      node_add(n, nc);
      nodes[nc].slot = (uint16_t)s;
      apply_key(nc, &k, idx);
      if (node_stream_hook) node_stream_hook(nc);
    } else if (!near && got && !(nodes[got].flags2 & NF2_KEEP)) {
      node_free(got);
    }
  }
}

/* ---------------------------------------------------------------- drawing */
static void draw_virtual(uint16_t sym, unsigned frame, Mat m, uint8_t alpha, int depth);

static void draw_key(const Key *k, Mat parent, uint8_t alpha, unsigned pframe, int depth) {
  if ((k->flags & K_HIDDEN) || !k->alpha) return;
  float a = 1, b = 0, c = 0, d = 1;
  if (k->mat) { const float *mm = mat(k->mat); a = mm[0]; b = mm[1]; c = mm[2]; d = mm[3]; }
  float rx = k->rx4 * 0.25f, ry = k->ry4 * 0.25f;
  Mat m = mat_mul(parent, (Mat){a, b, c, d, k->x - (rx * a + ry * c), k->y - (rx * b + ry * d)});
  uint8_t al = (uint8_t)((alpha * k->alpha + 127) / 255);
  if (k->kind == CK_SHAPE) { gfx_shape(payload(k->ref), m, al); return; }
  if (k->kind != CK_SYM || k->ref >= SYM_COUNT) return;
  SymInfo si;
  sym_info(k->ref, &si);
  if (si.type == SYM_BITMAP) gfx_sprite((uint16_t)si.v, m, al);
  else if (si.type == SYM_SHAPE) gfx_shape(payload((uint16_t)si.v), m, al);
  else if (si.type == SYM_CLIP) {
    unsigned f = 0;
    if (k->mode == MODE_SYNCHED) f = k->sp + pframe - k->start;
    else if (k->mode == MODE_SINGLE) f = k->sp;
    draw_virtual(k->ref, f, m, al, depth + 1);
  }
}

/* a clip that has no node (never happens for clips, kept for synched graphics) */
static void draw_virtual(uint16_t sym, unsigned frame, Mat m, uint8_t alpha, int depth) {
  Clip c;
  if (depth > 12 || !clip_get(sym, &c)) return;
  if (frame >= c.nframes) frame = c.nframes ? frame % c.nframes : 0;
  const uint8_t *q = c.slots;
  for (unsigned s = 0; s < c.nslots; s++) {
    Key k;
    uint16_t idx;
    q = slot_key(q, frame, &k, &idx);
    if (idx != NONE16 && !(k.flags & K_ABSENT)) draw_key(&k, m, alpha, frame, depth);
  }
}

static const char *text_of(NodeId n);
static int32_t text_color(NodeId n);
static int text_width(NodeId n);

/* a text node's text s (NULL: its own), placed by m (the view's), as the
 * doodle's canvas sets it: the size on screen picks the font's scale (in
 * thirds; small texts keep the crisp 10 px), the baseline its offset (the
 * canvas puts PixelMplus a font pixel higher than its rows), colour -1: the
 * node's own */
void node_text_draw(NodeId id, Mat m, const char *s, int32_t color, uint8_t alpha) {
  const Node *n = &nodes[id];
  if (n->kind != NK_TEXT || n->ref == NONE16 || !alpha) return;
  const uint8_t *t = payload(n->ref);
  if (!s) s = text_of(id);
  float scale = sqrtf(m.a * m.a + m.b * m.b), px = t[2] * scale;
  int k3 = px >= 11.5f ? (int)(px * 0.3f + .5f) : 3;
  int tw = text_width(id);
  int lw = (int)((tw ? tw : rds16(t + 8) * 0.25f) * scale), lh = (int)(rds16(t + 10) * 0.25f * scale + .5f);
  if (lh > 0 && lh < FONT_HEIGHT * k3 / 3) lh = FONT_HEIGHT * k3 / 3;   /* the lineHeight, at least a glyph (the dialogue's 11 px: its options follow its lines) */
  m.ty += (t[7] == 1 ? -6 : t[7] >= 2 ? -10 : -1) * k3 / 3;   /* top, middle, alphabetic */
  gfx_text_k3(s, m, color >= 0 ? (uint16_t)color : rgb565(t[3], t[4], t[5]), t[6], (int16_t)lw, (int16_t)lh, alpha, k3);
}

static void draw_rec(NodeId id, Mat parent, uint8_t alpha) {
  const Node *n = &nodes[id];
  if ((n->flags & (NF_VISIBLE | NF_ONSTAGE)) != (NF_VISIBLE | NF_ONSTAGE) || !n->alpha) return;
  Mat m = mat_mul(parent, node_local(id));
  uint8_t al = (uint8_t)((alpha * n->alpha + 127) / 255);
  if (id == node_draw_hook_id && node_draw_hook) { node_draw_hook(id, m, al); return; }
  switch (n->kind) {
    case NK_BITMAP: gfx_sprite(n->ref, m, al); return;
    case NK_SHAPE: if (n->ref != NONE16) gfx_shape(payload(n->ref), m, al); return;
    case NK_TEXT: {
      if (n->ref == NONE16) return;
      int32_t tc = text_color(id);
      node_text_draw(id, m, text_of(id), tc, al);
      return;
    }
    default: break;
  }
  if ((n->flags & NF_SORTED) || n->kind != NK_CLIP) {
    for (NodeId c = n->first; c; c = nodes[c].next) draw_rec(c, m, al);
    return;
  }
  /* timeline order: slots, each a node or drawn from the data; then code-added nodes */
  Clip c;
  NodeId ch = n->first;
  if (clip_get(n->sym, &c)) {
    const uint8_t *q = c.slots;
    for (unsigned s = 0; s < c.nslots; s++) {
      while (ch && nodes[ch].slot != NONE16 && nodes[ch].slot < s) ch = nodes[ch].next;
      if (ch && nodes[ch].slot == s) {
        unsigned nk;
        const uint8_t *r = read_varint(q, &nk);
        for (unsigned i = 0; i < nk; i++) { Key k; r = read_key(r, &k); }
        q = r;
        draw_rec(ch, m, al);
        continue;
      }
      Key k;
      uint16_t idx;
      q = slot_key(q, n->frame, &k, &idx);
      if (idx != NONE16 && !(k.flags & K_ABSENT) && !needs_node(&k)) draw_key(&k, m, al, n->frame, 0);
    }
  }
  for (NodeId x = n->first; x; x = nodes[x].next)
    if (nodes[x].slot == NONE16) draw_rec(x, m, al);
}

void node_draw(NodeId root, Mat m) { draw_rec(root, m, 255); }
void node_draw_in(NodeId n, Mat parent, uint8_t alpha) { draw_rec(n, parent, alpha); }
void node_draw_sym(uint16_t sym, unsigned frame, Mat m, uint8_t alpha) { draw_virtual(sym, frame, m, alpha, 0); }

/* ---------------------------------------------------------------- events */
void node_on(NodeId n, const char *event, EventFn fn, void *ctx) {
  int id = str_find(event);
  if (id < 0) return;
  for (int i = 0; i < EV_MAX; i++)
    if (!evs[i].n) { evs[i] = (Listener){n, (uint16_t)id, fn, ctx}; return; }
}

void node_off_all(NodeId n) {
  for (int i = 0; i < EV_MAX; i++) if (evs[i].n == n) evs[i].n = 0;
}

void node_emit(NodeId n, uint16_t ev) {
  for (int i = 0; i < EV_MAX; i++)
    if (evs[i].n == n && evs[i].ev == ev) evs[i].fn(n, ev, evs[i].ctx);
}

/* ---------------------------------------------------------------- texts */
static const char *text_of(NodeId n) {
  for (int i = 0; i < TX_MAX; i++) if (txs[i].n == n && txs[i].s) return txs[i].s;
  return nodes[n].ref != NONE16 ? str(rd16(payload(nodes[n].ref))) : "";
}

void node_set_text(NodeId n, const char *s) {
  for (int i = 0; i < TX_MAX; i++) if (txs[i].n == n) { txs[i].s = s; return; }
  for (int i = 0; i < TX_MAX; i++) if (!txs[i].n) { txs[i] = (Tx){n, 0, s, -1}; return; }
}

void node_set_text_color(NodeId n, uint16_t c) {
  for (int i = 0; i < TX_MAX; i++) if (txs[i].n == n) { txs[i].color = c; return; }
  for (int i = 0; i < TX_MAX; i++) if (!txs[i].n) { txs[i] = (Tx){n, 0, NULL, c}; return; }
}

void node_set_text_width(NodeId n, int16_t lw) {
  for (int i = 0; i < TX_MAX; i++) if (txs[i].n == n) { txs[i].lw = lw; return; }
  for (int i = 0; i < TX_MAX; i++) if (!txs[i].n) { txs[i] = (Tx){n, lw, NULL, -1}; return; }
}

static int text_width(NodeId n) {
  for (int i = 0; i < TX_MAX; i++) if (txs[i].n == n) return txs[i].lw;
  return 0;
}

static int32_t text_color(NodeId n) {
  for (int i = 0; i < TX_MAX; i++) if (txs[i].n == n) return txs[i].color;
  return -1;
}

const char *node_text(NodeId n) { return text_of(n); }
