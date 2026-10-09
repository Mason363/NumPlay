/* The display tree: a small CreateJS for the doodle's baked Animate clips.
 *
 * Nodes are clips (with a timeline), bitmaps, shapes, texts and plain
 * containers. A clip's children come from its timeline, one per slot; bitmaps
 * and unnamed shapes of a timeline are not nodes, they are drawn straight
 * from the data ("virtual" children). Code can add more children, move nodes,
 * play labels, and listen to the events clips dispatch. */
#ifndef CI_NODE_H
#define CI_NODE_H
#include "ci.h"
#include "gfx.h"

#ifndef NODE_MAX
#define NODE_MAX 320
#endif
typedef uint16_t NodeId;       /* 0 = none */

enum { NK_CLIP, NK_BITMAP, NK_SHAPE, NK_TEXT, NK_CONT };
enum {
  NF_PLAYING = 1, NF_VISIBLE = 2, NF_ONSTAGE = 4,   /* ONSTAGE: present in its parent's timeline now */
  NF_XFORM = 8,        /* scale/rotation set by code (see node_xform) */
  NF_SORTED = 16,      /* children in list order, all real nodes (sortChildren) */
  NF_TICK = 32,        /* tickEnabled */
  NF_ENTITY = 64,      /* has components */
  NF_USED = 128
};

typedef struct {
  float x, y;
  int16_t rx4, ry4;          /* registration point, 1/4 px */
  uint16_t mat;              /* 2x2 matrix from the timeline (0 = identity) */
  uint16_t sym;              /* symbol, or NONE16 */
  uint16_t frame;
  NodeId parent, first, next;
  uint16_t slot;             /* slot in the parent's timeline, NONE16 if added by code */
  uint16_t key;              /* last timeline key applied to this node (by its parent) */
  uint16_t name;             /* instance name */
  uint16_t T;                /* component table entry of its symbol */
  uint16_t ref;              /* bitmap: sprite; text/shape: payload */
  uint16_t ent;              /* entity registry slot + 1 (ent.c) */
  uint8_t kind, flags, flags2, alpha;
} Node;
/* flags2: how its parent's timeline drives it, streaming */
enum { NF2_LAZY = 1, NF2_KEEP = 2, NF2_NOLOOP = 4, NF2_MODE = 24 /* mode << 3 */, NF2_DYN = 32, NF2_FRESH = 64 /* its first tick is to come */,
       NF2_PARTIAL = 128 /* made by node_new_sym_frame: children made when first shown */ };
static inline uint8_t node_mode(const Node *n) { return (uint8_t)((n->flags2 & NF2_MODE) >> 3); }
static inline bool node_loops(const Node *n) { return !(n->flags2 & NF2_NOLOOP); }

extern Node *nodes;                  /* NODE_MAX of them, on main's stack on the calculator */
void node_set_memory(Node *mem);
static inline Node *N(NodeId id) { return &nodes[id]; }

void node_reset(void);
unsigned node_count(void);
NodeId node_new_sym(uint16_t sym);            /* instance of a symbol, with its subtree */
NodeId node_new(uint8_t kind);                /* empty container/shape/text */
NodeId node_new_sym_frame(uint16_t sym, int frame);  /* only the children that frame shows (menus) */
void node_free(NodeId n);                     /* removes it and its subtree */
void node_add(NodeId parent, NodeId child);   /* at the end (on top) */
void node_add_at(NodeId parent, NodeId child, NodeId before);
void node_remove(NodeId child);               /* detach, keep */
NodeId node_child(NodeId n, const char *name);    /* direct child with that instance name */
NodeId node_find(NodeId n, const char *name);     /* first descendant with that name */
bool node_named(NodeId n, const char *name);
int node_index(NodeId parent, NodeId child);
NodeId node_nth(NodeId parent, int i);

/* timeline */
bool node_goto(NodeId n, const char *label_or_null, int frame, bool play);
bool node_has_label(NodeId n, const char *label);
const char *node_label(NodeId n);              /* the current label ("" if none) */
int node_frames(NodeId n);
void node_play(NodeId n);
void node_stop(NodeId n);
void node_tick(NodeId root);                   /* advance playing clips by one frame (the Ticker) */
void node_update(NodeId root);                 /* apply timelines to children, run frame scripts */

/* transforms */
void node_xform(NodeId n, float sx, float sy, float rot);   /* code-set scale and rotation */
void node_get_xform(NodeId n, float *sx, float *sy, float *rot);
Mat node_local(NodeId n);
Mat node_global(NodeId n);                     /* up to the root */
Mat node_to(NodeId n, NodeId ancestor);        /* to the ancestor's space */
bool node_visible_chain(NodeId n);
static inline bool node_visible(NodeId n) { return (nodes[n].flags & (NF_VISIBLE | NF_ONSTAGE)) == (NF_VISIBLE | NF_ONSTAGE); }
void node_set_visible(NodeId n, bool v);

/* bounds in the node's own space: nominal bounds of its symbol or its shape */
bool node_bounds(NodeId n, float *x, float *y, float *w, float *h);
/* bounds of `n` in the space of `space` */
bool node_bounds_in(NodeId n, NodeId space, float *x, float *y, float *w, float *h);
bool node_hit(NodeId n, float gx, float gy);   /* point (in n's parent space... see node.c) in its shapes */

/* Big maps: a clip made with node_new_sym_lazy() gets its timeline children
 * only while they are near the rectangle given to node_stream() (in the
 * clip's own space); the others are freed unless marked NF2_KEEP. New ones
 * are added last (whatever the timeline order) and passed to the hook. */
NodeId node_new_sym_lazy(uint16_t sym);
void node_stream(NodeId n, float x0, float y0, float x1, float y1);
extern void (*node_stream_hook)(NodeId n);
extern uint16_t node_lazy_sym;                 /* instances of this symbol are made lazy */
/* A long movie (NF2_DYN) has nodes only for the children on stage now; they
 * are made and freed as it plays (new ones go through node_stream_hook). */
NodeId node_new_sym_dynamic(uint16_t sym);
/* A clip made by node_new_sym_frame (a room, a menu) has nodes only for what
 * its frame showed; a child a later frame shows is made then, and kept (new
 * ones go through node_partial_hook). */
extern void (*node_partial_hook)(NodeId n);
extern uint16_t node_dynamic_sym;              /* instances of this symbol are made dynamic */

/* a clip's timeline children at a frame, without nodes: symbol, instance
 * name, origin and bounds in the clip's space; fn returns false to stop */
typedef struct { uint16_t sym, name; float x, y, bx, by, bw, bh; bool has_bounds; } SlotInfo;
void clip_each_slot(uint16_t sym, unsigned frame, bool (*fn)(unsigned slot, const SlotInfo *si, void *ctx), void *ctx);
/* where a timeline child of n would be (bounds in n's space), for slot s */
bool node_slot_bounds(NodeId n, unsigned slot, float *x, float *y, float *w, float *h);

/* drawing */
void node_draw(NodeId root, Mat m);
void node_text_draw(NodeId text, Mat m, const char *s, int32_t color, uint8_t alpha);   /* s NULL: its own, colour -1: its own */
void node_draw_in(NodeId n, Mat parent, uint8_t alpha);   /* n in a parent drawn with `parent` */
void node_draw_sym(uint16_t sym, unsigned frame, Mat m, uint8_t alpha);   /* a symbol's frame, without nodes */
/* one node drawn by code instead (its matrix and alpha computed): the island's map */
extern NodeId node_draw_hook_id;
extern void (*node_draw_hook)(NodeId n, Mat m, uint8_t alpha);

/* called when a node is freed (ent.c forgets its entities) */
extern void (*node_free_hook)(NodeId n);
/* the first child that is on stage now (after a label change, the new pose) */
NodeId node_onstage_child(NodeId n);
void node_update_one(NodeId n);               /* apply n's timeline to its children now */

/* events dispatched by frame scripts: this.dispatchEvent / this.parent.dispatchEvent */
typedef void (*EventFn)(NodeId target, uint16_t event, void *ctx);
void node_on(NodeId n, const char *event, EventFn fn, void *ctx);
void node_off_all(NodeId n);
void node_emit(NodeId n, uint16_t event);

/* text nodes */
void node_set_text(NodeId n, const char *s);   /* string kept by pointer */
void node_set_text_color(NodeId n, uint16_t rgb565);
void node_set_text_width(NodeId n, int16_t lw);   /* lineWidth set by code, in its own px (0: as designed) */
const char *node_text(NodeId n);
#endif
